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
{ /* we don't check against max/min, because of the cached high/lows */
  plevel low, high;

  for(low = this->_low_buy_stop ; low <= this->_last ; ++low)    
    this->_handle_triggered_stop_chain(low,false); 

  for(high = this->_high_sell_stop ; high >= this->_last ; --high)
    this->_handle_triggered_stop_chain(high,true);  
}

SOB_TEMPLATE
void SOB_CLASS::_handle_triggered_stop_chain(plevel plev, bool ask_side)
{
  stop_chain_type cchain;
  plevel limit;  
  /*
   * need to copy the relevant chain, delete original, THEN insert
   * if not we can hit the same order more than once / go into infinite loop
   */
  cchain = stop_chain_type(plev->second);
  plev->second.clear();

  if(!cchain.empty()){ /* <-- before we potentially recurse into new orders */
    if(ask_side)
      this->_high_sell_stop = plev - 1;
    else
      this->_low_buy_stop = plev + 1;
  }

  for(stop_chain_type::value_type& e : cchain){
    limit = (plevel)std::get<1>(e.second);
   /*
    * note below we are calling the private versions of _insert,
    * so we can use the old order id as the new one; this allows caller
    * to maintain control via the same order id
    */
    if(limit){
      this->_insert_limit_order(std::get<0>(e.second), limit, 
                                std::get<2>(e.second), std::get<3>(e.second), 
                                e.first);
    }else
      this->_insert_market_order(std::get<0>(e.second), std::get<2>(e.second), 
                                 std::get<3>(e.second), e.first);   
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

  long rmndr = 0;
  inside = this->_ask;  
         
  while( (inside <= plev || !plev) && size > 0 && (inside < this->_end) )
  {     
    del_iter = inside->first.begin();

    for(limit_chain_type::value_type& elem : inside->first){
      /* check each order , FIFO, for that price level
       * if here we must fill something */
      for_sale = elem.second.first;
      rmndr = size - for_sale;
      amount = std::min(size,for_sale);
      this->_trade_has_occured(inside, amount, id, elem.first, callback, 
                               elem.second.second, true);    
      /* reduce the amount left to trade */
      size -= amount;
      /* if we don't need all, adjust the outstanding order size,
       * otherwise indicate order should be removed from the chain */
      if(rmndr < 0)
        elem.second.first -= amount;
      else
        ++del_iter;      
      if(size <= 0)
        break; /* if we have nothing left */
    }
    inside->first.erase(inside->first.begin(),del_iter);     
    
    for( ; inside->first.empty() && inside < this->_end; ++inside)
      { /* if we we're on an empty chain 'jump' to one that isn't */     
      }      
    this->_ask = inside; /* @ +1 the beg if we went all the way ! */
    
    if(inside >= this->_end){
      this->_ask_size = 0;    
      break;
    }else
      this->_ask_size = this->_chain_size(&this->_ask->first);    
  }
  return size; /* what we couldn't fill */
}

SOB_TEMPLATE 
size_type SOB_CLASS::_hit_bids(plevel plev,
                               id_type id,
                               size_type size,
                               fill_callback_type& callback)
{
  limit_chain_type::iterator del_iter;
  size_type amount, to_buy;
  plevel inside;

  long rmndr = 0;
  inside = this->_ask;  
         
  while( (inside >= plev || !plev) && size > 0 && (inside >= this->_beg) )
  {     
    del_iter = inside->first.begin();

    for(limit_chain_type::value_type& elem : inside->first){
      /* check each order , FIFO, for that price level
       * if here we must fill something */
      to_buy = elem.second.first;
      rmndr = size - to_buy;
      amount = std::min(size,to_buy);
      this->_trade_has_occured(inside, amount, id, elem.first, callback, 
                               elem.second.second, true);  
      /* reduce the amount left to trade */
      size -= amount;
      /* if we don't need all, adjust the outstanding order size,
       * otherwise indicate order should be removed from the chain */
      if(rmndr < 0)
        elem.second.first -= amount;
      else
        ++del_iter;      
      if(size <= 0)
        break; /* if we have nothing left */
    }
    inside->first.erase(inside->first.begin(),del_iter);
    
    for( ; inside->first.empty() && inside >= this->_beg; --inside)
      { /* if we we're on an empty chain 'jump' to one that isn't */    
      }      
    this->_bid = inside; /* @ -1 the beg if we went all the way ! */
    
    if(inside < this->_beg){
      this->_bid_size = 0;    
      break;
    }else
      this->_bid_size = this->_chain_size(&this->_bid->first);   }
  return size; /* what we couldn't fill */
}


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
  else if(!buy && limit <= this->_bid)
    rmndr = this->_hit_bids(limit,id,size,callback);

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

SOB_TEMPLATE
void SOB_CLASS::_insert_market_order(bool buy,
                                     size_type size,
                                     fill_callback_type callback,
                                     id_type id)
{
  size_type rmndr = size;

  rmndr = buy ? this->_lift_offers(nullptr,id,size,callback)
              : this->_hit_bids(nullptr,id,size,callback);
  if(rmndr)
    throw liquidity_exception("market order couldn't fill");

  this->_on_trade_completion();
}

SOB_TEMPLATE
void SOB_CLASS::_insert_stop_order(bool buy,
                                   plevel stop,
                                   size_type size,
                                   fill_callback_type callback,
                                   id_type id)
{
  this->_insert_stop_order(buy, stop, nullptr, size, std::move(callback), id);
}

SOB_TEMPLATE
void SOB_CLASS::_insert_stop_order(bool buy,
                                   plevel stop,
                                   plevel limit,
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
   */
  stop_chain_type* orders = &stop->second;

  /* use 0 limit price for market order */
  stop_bndl_type bndl = stop_bndl_type(buy,(void*)limit,size,callback);
  orders->insert(stop_chain_type::value_type(id,std::move(bndl)));
  /*
   * we maintain references to the most extreme stop prices so we can
   * avoid searching the entire array for triggered orders
   *
   * adjust cached values if ncessary; (should we just maintain a pointer ??)
   */
  if(buy && stop < this->_low_buy_stop)
    this->_low_buy_stop = stop;
  else if(!buy && stop > this->_high_sell_stop)
    this->_high_sell_stop = stop;

  this->_on_trade_completion();
}

SOB_TEMPLATE
typename SOB_CLASS::plevel SOB_CLASS::_ptoi(price_type price) const
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
price_type SOB_CLASS::_itop(plevel plev) const
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
    
    for(MarketMaker& mm : mms)
      mm.initialize(this);
    
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

SOB_TEMPLATE
id_type SOB_CLASS::insert_market_order(bool buy,
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

SOB_TEMPLATE
id_type SOB_CLASS::insert_stop_order(bool buy,
                                     price_type stop,
                                     size_type size,
                                     fill_callback_type callback)
{
  return this->insert_stop_order(buy,stop,0,size,callback);
}

SOB_TEMPLATE
id_type SOB_CLASS::insert_stop_order(bool buy,
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

  id = this->_generate_id();

  this->_insert_stop_order(buy,this->_ptoi(stop),this->_ptoi(limit),size,
                           callback,id);
  return id;
}

SOB_TEMPLATE
bool SOB_CLASS::pull_order(id_type id)
{ 
  // search between min(low_buy_lim,low_sell_stp) and max(high_buy_lim,high_buy_stop)
  // get the cache updates working first, 
  // for now just search the whole array(either way should start from _last and go out)
  for(chain_pair_type& e : this->_book){
    for(const limit_chain_type::value_type& lc : e.first )
      if(lc.first == id){
        e.first.erase(id);
        return true;
      }
    for(const stop_chain_type::value_type& sc : e.second )
      if(sc.first == id){
        e.second.erase(id);
        return true;
      }    
  }
  return false;
}


SOB_TEMPLATE
id_type SOB_CLASS::replace_with_limit_order(id_type id,
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

SOB_TEMPLATE
id_type SOB_CLASS::replace_with_market_order(id_type id,
                                             bool buy,
                                             size_type size,
                                             fill_callback_type callback)
{
  id_type id_new = 0;
  
  if(this->pull_order(id))
    id_new =  this->insert_market_order(buy,size,callback);
  
  return id_new;
}

SOB_TEMPLATE
id_type SOB_CLASS::replace_with_stop_order(id_type id,
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

SOB_TEMPLATE
id_type SOB_CLASS::replace_with_stop_order(id_type id,
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


SOB_TEMPLATE
std::string SOB_CLASS::timestamp_to_str(const SOB_CLASS::time_stamp_type& tp)
{
  std::time_t t = clock_type::to_time_t(tp);
  std::string ts = std::ctime(&t);
  ts.resize(ts.size() -1);
  return ts;
}

};