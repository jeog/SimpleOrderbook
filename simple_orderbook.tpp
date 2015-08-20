namespace NativeLayer{



SOB_TEMPLATE
void SOB_CLASS::_on_trade_completion()
{
  if(this->_is_dirty){
    this->_is_dirty = false;
    while(!this->_deferred_callback_queue.empty()){
      dfrd_cb_elem_type e = this->_deferred_callback_queue.front();
      std::get<0>(e)(std::get<2>(e),std::get<4>(e),std::get<5>(e));
      std::get<1>(e)(std::get<3>(e),std::get<4>(e),std::get<5>(e));
      this->_deferred_callback_queue.pop();
    }
    this->_look_for_triggered_stops();
  }
}

SOB_TEMPLATE
void SOB_CLASS::_trade_has_occured(plevel plev,
                                 size_type size,
                                 id_type idbuy,
                                 id_type idsell,
                                 fill_callback_type& cbbuy,
                                 fill_callback_type& cbsell,
                                 bool took_offer)
{/*
  * CAREFUL: we can't insert orders from here since we have yet to finish
  * processing the initial order (possible infinite loop);
  *
  * adjust state and use _on_trade_completion() method for earliest insert
  */
  this->_deferred_callback_queue.push(
    dfrd_cb_elem_type(cbbuy, cbsell, idbuy, idsell, this->_itop(plev), size));

  if(this->_t_and_s_full)
    this->_t_and_s.pop_back();
  else if(this->_t_and_s.size() >= (this->_t_and_s_max_sz - 1))
    this->_t_and_s_full = true;

  this->_t_and_s.push_back(
    t_and_s_type(clock_type::now(),this->_itop(plev),size));

  this->_last = plev;
  this->_total_volume += size;
  this->_last_size = size;
  this->_is_dirty = true;
}

/*
 ************************************************************************
 ************************************************************************
 *** CURRENTLY working under the constraint that stop priority goes:  ***
 ***   low price to high for buys                                     ***
 ***   high price to low for sells                                    ***
 ***   buys before sells                                              ***
 ***                                                                  ***
 *** The other possibility is FIFO irrespective of price              ***
 ************************************************************************
 ************************************************************************
 */

SOB_TEMPLATE
void SOB_CLASS::_look_for_triggered_stops()
{ /* 
   * we don't check against max/min, because of the cached high/lows 
   */
  plevel low, high;

  for(low = this->_low_buy_stop ; low < this->_last ; ++low)    
    this->_handle_triggered_stop_chain(low,false); 

  for(high = this->_high_sell_stop ; high > this->_last ; --high)
    this->_handle_triggered_stop_chain(high,true);  
}

SOB_TEMPLATE
void SOB_CLASS::_handle_triggered_stop_chain(plevel plev, bool ask_side)
{
  stop_chain_type cchain;
  price_type limit;  
  /*
   * need to copy the relevant chain, delete original, THEN insert
   * if not we can hit the same order more than once / go into infinite loop
   */
  cchain = stop_chain_type(plev->second);
  plev->second.clear();

  if(!cchain.empty()){
   /*
    * need to do this before we potentially recurse into new orders
    */
    if(ask_side)
      this->_high_sell_stop = plev - 1;
    else
      this->_low_buy_stop = plev + 1;
  }

  for(stop_chain_type::value_type& e : cchain){
    limit = std::get<1>(e.second).first;
   /*
    * note below we are calling the private versions of _insert,
    * so we can use the old order id as the new one; this allows caller
    * to maintain control via the same order id
    */
    if(limit > 0)
      this->_insert_limit_order(!ask_side, this->_ptoi(limit), 
                                 std::get<1>(e.second).second, 
                                 std::get<2>(e.second), e.first);
  //  else
 //     this->_insert_market_order(!ask_side, elem.second.first.second,
  //                               elem.second.second, elem.first);
  }
}

/*
 ****************************************************************************
 ****************************************************************************
 *** _lift_offers / _hit_bids are the guts of order execution: attempting ***
 *** to match limit/market orders against the order book, adjusting       ***
 *** state, checking for overflows, signaling etc.                        ***
 ***                                                                      ***
 *** 1) All the corner cases haven't been tested                          ***
 *** 2) Code is quite kludgy and overwritten, needs to be cleaned-up      ***
 ****************************************************************************
 ****************************************************************************
 */
SOB_TEMPLATE 
size_type SOB_CLASS::_lift_offers(plevel plev,
                                  id_type id,
                                  size_type size,
                                  fill_callback_type& callback)
{
  limit_chain_type::iterator del_iter;
  size_type amount, for_sale;
  plevel inside;

  bool tflag = false;
  long rmndr = 0;
  inside = this->_ask;  
         
  while( (inside < plev || !plev) 
         && size > 0       
         && (inside <= this->_end) )
  {     
    del_iter = inside->first.begin();

    for(limit_chain_type::value_type& elem : inside->first){
      /* check each order , FIFO, for that price level
       * if here we must fill something */
      for_sale = elem.second.first;
      rmndr = size - for_sale;
      amount = std::min(size,for_sale);
      this->_trade_has_occured(inside, amount, id, elem.first,
                               callback, elem.second.second, true);
      tflag = true;
      /* reduce the amount left to trade */
      size -= amount;
      /* if we don't need all adjust the outstanding order size,
       * otherwise indicate order should be removed from the maping */
      if(rmndr < 0)
        elem.second.first -= amount;
      else
        ++del_iter;
      /* if we have nothing left, break early */
      if(size <= 0)
        break;
    }
    inside->first.erase(inside->first.begin(),del_iter);

    try{ /* incase a market order runs the whole array, past max */
      ++inside;
    }catch(std::exception& e){
      this->_ask = inside;
      this->_ask_size = this->_chain_size(&(this->_ask->first));
      break;
    }

    if(inside->first.empty())
      this->_ask = inside;

    if(tflag){
      tflag = false;
      this->_ask_size = this->_chain_size(&(this->_ask->first));
    }
    
  }
  /* if we finish on an empty chain look for one that isn't */
  if(inside->first.empty()){ 
    for( ; inside->first.empty() && inside < this->_end; 
        ++inside)
    {      
    }
    --inside; // <- make sure we get back in the range !
    this->_ask = inside;
    this->_ask_size = this->_chain_size((&inside->first));
  }
  return size; /* what we couldn't fill */
}
/*

size_type SimpleOrderbook::_hit_bids(price_type price,
                                     id_type id,
                                     size_type size,
                                     fill_callback_type& callback )
{
  limit_chain_type *pibid, *pmin;
  limit_chain_type::iterator del_iter;
  size_type amount, for_bid;
  price_type inside_price;

  bool tflag = false;
  long rmndr = 0;
  inside_price = this->_bid;
  pibid = nullptr;

          price <= inside_price (or <= 0) with error allowance 
  while( ((price - inside_price) < this->_incr_err || price <= 0)
         && size > 0
          inside_price >= max_price with error allowance 
         && (inside_price - this->_beg) > -this->_incr_err )
  {
    pibid = this->_find_order_chain(this->_bid_limits,inside_price);
    del_iter = pibid->begin();

    for(limit_chain_type::value_type& elem : *pibid){
       check each order , FIFO, for that price level
       * if here we must fill something 
      for_bid = elem.second.first;
      rmndr = size - for_bid;
      amount = std::min(size,for_bid);
      this->_trade_has_occured(inside_price, amount, elem.first, id,
                               elem.second.second, callback, false);
      tflag = true;
       reduce the amount left to trade 
      size -= amount;
       if we don't need all adjust the outstanding order size,
       * otherwise indicate order should be removed from the maping 
      if(rmndr < 0)
        elem.second.first -= amount;
      else
        ++del_iter;
       if we have nothing left, break early 
      if(size <= 0)
        break;
    }
    pibid->erase(pibid->begin(),del_iter);

    try{
       in case a market order runs the whole array, past min/
      inside_price = this->_align(inside_price - this->_incr);
    }catch(std::range_error& e){
      this->_bid = inside_price;
      this->_bid_size = this->_chain_size(this->_bid_limits, this->_bid);
      break;
    }

    if(pibid->empty())
      this->_bid = inside_price;

    if(tflag){
      tflag = false;
      this->_bid_size = this->_chain_size(this->_bid_limits, this->_bid);
    }
  }
   if we finish on an empty chain look for one that isn't 
  if(pibid && pibid->empty())
  {
    for( --pibid, pmin = &(this->_bid_limits[0]) ;
         pibid->empty() && pibid > pmin ;
         --pibid)
    {
      inside_price = this->_align(inside_price - this->_incr);
    }
    this->_bid = inside_price;
    this->_bid_size = this->_chain_size(this->_bid_limits, this->_bid);
  }
  return size;  what we couldn't fill 
}
*/

/*
void SimpleOrderbook::_init(size_type levels_from_init, size_type end_from_init)
{
  /* (crudely, for the time being,) initialize market makers *//*
  limit_order_type order;
  size_type aanchor = this->_last / this->_incr - 1;

  for(MarketMaker& elem : this->_market_makers)
  {
    for(size_type i = aanchor - levels_from_init;
        i < aanchor -end_from_init;
        ++i){
      order = elem.post_bid(this->_itop(i));
      this->insert_limit_order(true, order.first, order.second,
                               &MarketMaker::default_callback);
    }
    for(size_type i = aanchor + end_from_init;
        i < aanchor + levels_from_init;
        ++i){
      order = elem.post_bid(this->_itop(i));
      this->insert_limit_order(false, order.first, order.second,
                               &MarketMaker::default_callback);
    }
  }
}
*/
SOB_TEMPLATE
void SOB_CLASS::_insert_limit_order(bool buy,
                                    plevel limit,
                                    size_type size,
                                    fill_callback_type callback,
                                    id_type id)
{
  size_type rmndr = size; 
  /*
   * first look if there are matching orders on the offer side
   * pass ref to callback functor, we'll copy later if necessary
   */
  if(buy && limit >= this->_ask)
    rmndr = this->_lift_offers(limit,id,size,callback);
//  else if(!buy && limit <= this->_bid)
 //   rmndr = this->_hit_bids(plev,id,size,callback);

  /*
   * then add what remains to bid side; copy callback functor, needs to persist
   */
  if(rmndr > 0){
    limit_chain_type* orders = &limit->first;

    limit_bndl_type bndl = limit_bndl_type(rmndr,callback);
    orders->insert(limit_chain_type::value_type(id,std::move(bndl)));

    if(buy && limit >= this->_bid){
      this->_bid = limit;
      this->_bid_size = this->_chain_size(orders);
    }else if(!buy && limit <= this->_ask){
      this->_ask = limit;
      this->_ask_size = this->_chain_size(orders);
    }

    if(buy && limit < this->_low_buy_limit)
      this->_low_buy_limit = limit;
    else if(!buy && limit > this->_high_sell_limit)
      this->_high_sell_limit = limit;
  }

  this->_on_trade_completion();
}

/*
void SimpleOrderbook::_insert_market_order(bool buy,
                                           size_type size,
                                           fill_callback_type callback,
                                           id_type id)
{
  size_type rmndr = size;

  rmndr = buy ? this->_lift_offers(-1,id,size,callback)
              : this->_hit_bids(-1,id,size,callback);
  if(rmndr)
    throw liquidity_exception("market order couldn't fill");

  this->_on_trade_completion();
}

void SimpleOrderbook::_insert_stop_order(bool buy,
                                          price_type stop,
                                          size_type size,
                                          fill_callback_type callback,
                                          id_type id)
{
  this->_insert_stop_order(buy, stop, 0, size, std::move(callback), id);
}

void SimpleOrderbook::_insert_stop_order(bool buy,
                                         price_type stop,
                                         price_type limit,
                                         size_type size,
                                         fill_callback_type callback,
                                         id_type id)
{ /*
   * we need an actual trade @/through the stop, i.e can't assume
   * it's already been triggered by where last/bid/ask is...
   *
   * simply pass the order to the appropriate stop chain
   *
   * copy callback functor, needs to persist
   *//*
  stop_chain_type* orders =
    this->_find_order_chain(buy ? this->_bid_stops : this->_ask_stops, stop);

  /* use 0 limit price for market order *//*
  stop_bndl_type bndl = stop_bndl_type( limit_order_type(limit,size),callback);
  orders->insert(stop_chain_type::value_type(id,std::move(bndl)));
  /*
   * we maintain references to the most extreme stop prices so we can
   * avoid searching the entire array for triggered orders
   *
   * adjust cached values if ncessary; (should we just maintain a pointer ??)
   *//*
  if(buy && stop < this->_low_buy_stop)
    this->_low_buy_stop = stop;
  else if(!buy && stop > this->_high_sell_stop)
    this->_high_sell_stop = stop;

  this->_on_trade_completion();
}
*/
SOB_TEMPLATE
typename SOB_CLASS::plevel SOB_CLASS::_ptoi(price_type price)
{
  plevel plev;
  price_type incr_offset;

  incr_offset = price / ((price_type)incr_r::num/incr_r::den);
  plev = this->_beg + (size_type)round(incr_offset)-1;

  if(plev < this->_beg)
    throw std::range_error( "chain_pair_type* < _beg" );

  if(plev >= this->_end )
    throw std::range_error( "plevel >= _end" );

  return plev;
}

SOB_TEMPLATE 
price_type SOB_CLASS::_itop(plevel plev)
{
  price_type price, incr_offset;
  long long offset;

  if(plev < this->_beg)
    throw std::range_error( "plevel < _beg" );

  if(plev >= this->_end )
    throw std::range_error( "plevel >= _end" );

  offset = plev - this->_beg;
  incr_offset = offset * (price_type)incr_r::num / incr_r::den;
  price = (incr_offset*base_r::den + base_r::num) / base_r::den;

  return price; //floor(price*incr_denom) / incr_denom; 
}

SOB_TEMPLATE 
SOB_CLASS::SimpleOrderbook(std::vector<MarketMaker>& mms)
  :
  _bid_size(0),
  _ask_size(0),
  _last_size(0), 
  _book(), 
  _beg( _book.begin() ),
  _end( _book.end()), /* note: half-open range */
  _last( _book.begin() + lower_increments), 
  _bid( this->_beg ),
  _ask( this->_end ),
  _low_buy_limit( this->_last ),
  _high_sell_limit( this->_last ),
  _low_buy_stop( this->_end ),
  _high_sell_stop(  this->_beg ),
  _total_volume(0),
  _last_id(0),
  _market_makers( mms ), /* do we want to copy,borrow or steal?? */
  _is_dirty(false),
  _deferred_callback_queue(),
  _t_and_s(),
  _t_and_s_max_sz(1000),
  _t_and_s_full(false)
  {
    this->_t_and_s.reserve(this->_t_and_s_max_sz);
    //this->_init(mm_init_levels,2);
    std::cout<< "+ SimpleOrderbook Created\n";
  }

SOB_TEMPLATE 
SOB_CLASS::~SimpleOrderbook()
{
  std::cout<< "- SimpleOrderbook Destroyed\n";
}

SOB_TEMPLATE
id_type SOB_CLASS::insert_limit_order(bool buy,
                                      price_type limit,
                                      size_type size,
                                      fill_callback_type callback)
{
  id_type id;
  plevel plev;

  if(!this->_check_order_price(limit))
    throw invalid_order("invalid order price");

  if(!this->_check_order_size(size))
    throw invalid_order("invalid order size");

  id = this->_generate_id();
  plev = this->_ptoi(limit);

  this->_insert_limit_order(buy,plev,size,callback,id);
  return id;
}

/*
id_type SimpleOrderbook::insert_market_order(bool buy,
                                             size_type size,
                                             fill_callback_type callback)
{
  id_type id;

  if(!this->_check_order_size(size))
    throw invalid_order("invalid order size");

  id = this->_generate_id();

  this->_insert_market_order(buy,size,callback,id);
  return id;
}


id_type SimpleOrderbook::insert_stop_order(bool buy,
                                           price_type stop,
                                           size_type size,
                                           fill_callback_type callback)
{
  return this->insert_stop_order(buy,stop,0,size,callback);
}

id_type SimpleOrderbook::insert_stop_order(bool buy,
                                           price_type stop,
                                           price_type limit,
                                           size_type size,
                                           fill_callback_type callback)
{
  id_type id;

  if(!this->_check_order_price(stop))
    throw invalid_order("invalid stop price");

  if(!this->_check_order_size(size))
    throw invalid_order("invalid order size");

  if(limit > 0)
    limit = this->_align(limit);
  stop = this->_align(stop);
  id = this->_generate_id();

  this->_insert_stop_order(buy,stop,limit,size,callback,id);
  return id;
}

bool SimpleOrderbook::pull_order(id_type id)
{
  return this->_remove_order_from_chain_array(this->_bid_limits,id) ||
         this->_remove_order_from_chain_array(this->_ask_limits,id) ||
         this->_remove_order_from_chain_array(this->_bid_stops,id) ||
         this->_remove_order_from_chain_array(this->_ask_stops,id);
}

id_type SimpleOrderbook::replace_with_limit_order(id_type id,
                                                  bool buy,
                                                  price_type limit,
                                                  size_type size,
                                                  fill_callback_type callback)
{
  id_type id_new = 0;
  if(this->pull_order(id))
    id_new = this->insert_limit_order(buy,limit,size,callback);
  return id_new;
}

id_type SimpleOrderbook::replace_with_market_order(id_type id,
                                                   bool buy,
                                                   size_type size,
                                                   fill_callback_type callback)
{
  id_type id_new = 0;
  if(this->pull_order(id))
    id_new =  this->insert_market_order(buy,size,callback);
  return id_new;
}

id_type SimpleOrderbook::replace_with_stop_order(id_type id,
                                                 bool buy,
                                                 price_type stop,
                                                 size_type size,
                                                 fill_callback_type callback)
{
  id_type id_new = 0;
  if(this->pull_order(id))
    id_new = this->insert_stop_order(buy,stop,size,callback);
  return id_new;
}

id_type SimpleOrderbook::replace_with_stop_order(id_type id,
                                                 bool buy,
                                                 price_type stop,
                                                 price_type limit,
                                                 size_type size,
                                                 fill_callback_type callback)
{
  id_type id_new = 0;
  if(this->pull_order(id))
    id_new = this->insert_stop_order(buy,stop,limit,size,callback);
  return id_new;
}
*/


SOB_TEMPLATE
std::string SOB_CLASS::timestamp_to_str(const SOB_CLASS::time_stamp_type& tp)
{
  std::time_t t = clock_type::to_time_t(tp);
  std::string ts = std::ctime(&t);
  ts.resize(ts.size() -1);
  return ts;
}

};