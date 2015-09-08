#include "types.hpp"
#include "simple_orderbook.hpp"

namespace NativeLayer{
namespace SimpleOrderbook{

SOB_TEMPLATE
void SOB_CLASS::_on_trade_completion()
{
  if(this->_is_dirty){
    this->_is_dirty = false;    
    while(!this->_deferred_callback_queue.empty()){
      dfrd_cb_elem_type e = this->_deferred_callback_queue.front();
      this->_deferred_callback_queue.pop();
      /* BE SURE TO POP BEFORE WE CALL BACK */
      std::get<0>(e)(callback_msg::fill,std::get<2>(e),std::get<4>(e),
                     std::get<5>(e));
      std::get<1>(e)(callback_msg::fill,std::get<3>(e),std::get<4>(e),
                     std::get<5>(e));      
    }  
    this->_look_for_triggered_stops(); 
  }
}

SOB_TEMPLATE
void SOB_CLASS::_exec_order_queue()
{  
  order_queue_elem_type e;
  id_type id;
  while(true){
    {
      std::unique_lock<std::mutex> _(this->_queue_mutex);    
      this->_in_signal.wait(_, [this]{ return !this->_order_queue.empty();} );
   //   std::cout<< "CONUMSER START\n";
      e = std::move(this->_order_queue.front());
      this->_order_queue.pop();
    }      
    
    id = std::get<6>(e);
    if(id == 0) /* might want to protect this at some point */
      id = this->_generate_id();
    
    switch(std::get<0>(e)){
    case order_type::limit:
      {
        this->_insert_limit_order(std::get<1>(e),std::get<2>(e),std::get<4>(e),
                                  std::get<5>(e),id,std::get<7>(e)); 
      }
      break;
    case order_type::market:
      {
        this->_insert_market_order(std::get<1>(e),std::get<4>(e),std::get<5>(e),
                                   id);        
      }
      break;
    case order_type::stop:
      {
        this->_insert_stop_order(std::get<1>(e),std::get<3>(e),std::get<4>(e),
                                 std::get<5>(e),id);
      }
      break;
    case order_type::stop_limit:
      {
        this->_insert_stop_order(std::get<1>(e),std::get<3>(e), std::get<2>(e),
                                 std::get<4>(e),std::get<5>(e), id);         
      }
      break;
    default:
      throw std::runtime_error("invalid order type in order_queue");
    }    
   
    std::get<8>(e).set_value(id);  
  }  
}

SOB_TEMPLATE
id_type SOB_CLASS::_push_order_and_wait(order_type oty, 
                                        bool buy, 
                                        plevel limit,
                                        plevel stop,
                                        size_type size,
                                        callback_type cb,                                         
                                        post_exec_callback_type pe_cb,
                                        id_type id)
{
  id_type ret_id;
  std::promise<id_type> p;
  std::future<id_type> f(p.get_future());
  
  {
     std::lock_guard<std::mutex> _(this->_queue_mutex);
     this->_order_queue.push( 
       order_queue_elem_type(oty,buy,limit,stop,size,cb,id,pe_cb,std::move(p)));
  }
  
  this->_in_signal.notify_one();
  ret_id = f.get();
  this->_on_trade_completion();
  
  return ret_id;
}

SOB_TEMPLATE
void SOB_CLASS::_trade_has_occured(plevel plev,
                                   size_type size,
                                   id_type idbuy,
                                   id_type idsell,
                                   callback_type& cbbuy,
                                   callback_type& cbsell,
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
    this->_handle_triggered_stop_chain<false>(low); 

  for(high = this->_high_sell_stop ; high >= this->_last ; --high)
    this->_handle_triggered_stop_chain<true>(high);  
}

SOB_TEMPLATE
template<bool BuyStops>
void SOB_CLASS::_handle_triggered_stop_chain(plevel plev)
{
  stop_chain_type cchain;
  plevel limit;   
  /*
   * need to copy the relevant chain, delete original, THEN insert
   * if not we can hit the same order more than once / go into infinite loop
   */
  cchain = stop_chain_type(plev->second);
  plev->second.clear();

  if(BuyStops){
    this->_low_buy_stop = plev + 1;
    if(this->_low_buy_stop > this->_high_buy_stop)
      this->_low_buy_stop = this->_high_buy_stop = this->_end;
  }
  else{
    this->_high_sell_stop = plev - 1;
    if(this->_high_sell_stop < this->_low_sell_stop)
      this->_high_sell_stop = this->_low_sell_stop = this->_beg;
  }

  for(stop_chain_type::value_type& e : cchain){
    limit = (plevel)std::get<1>(e.second);
   /*
    * note below we are keeping the old id
    */  
    limit ? this->_push_order_and_wait(order_type::limit, std::get<0>(e.second),
                                       limit,nullptr,std::get<2>(e.second),
                                       std::get<3>(e.second), nullptr, e.first)
          : this->_push_order_and_wait(order_type::market, std::get<0>(e.second),
                                       nullptr, nullptr, std::get<2>(e.second), 
                                       std::get<3>(e.second), nullptr, e.first);
  }
  this->_on_trade_completion(); // <- this could be an issue
}

/*
 ****************************************************************************
 ****************************************************************************
 *** _lift_offers / _hit_bids are the guts of order execution: attempting ***
 *** to match limit/market orders against the order book, adjusting       ***
 *** state, checking for overflows, signaling etc.                        ***
 ***                                                                      ***
 *** ... NEEDS MORE TESTING ...                                           ***
 ****************************************************************************
 ****************************************************************************
 */
SOB_TEMPLATE 
size_type SOB_CLASS::_lift_offers(plevel plev,
                                  id_type id,
                                  size_type size,
                                  callback_type& callback)
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
    
    /* adjust cached val */
    if(this->_ask > this->_high_sell_limit)
      this->_high_sell_limit = this->_ask;
  }
  return size; /* what we couldn't fill */
}

SOB_TEMPLATE 
size_type SOB_CLASS::_hit_bids(plevel plev,
                               id_type id,
                               size_type size,
                               callback_type& callback)
{
  limit_chain_type::iterator del_iter;
  size_type amount, to_buy;
  plevel inside;

  long rmndr = 0;
  inside = this->_bid;  
         
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
      this->_bid_size = this->_chain_size(&this->_bid->first);   
    
    /* adjust cached val */
    if(this->_bid < this->_low_buy_limit)
      this->_low_buy_limit = this->_bid;
  }
  return size; /* what we couldn't fill */
}


SOB_TEMPLATE
void SOB_CLASS::_insert_limit_order(bool buy,
                                    plevel limit,
                                    size_type size,
                                    callback_type callback,
                                    id_type id,
                                    post_exec_callback_type plccb)
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
    
    if(buy)
    {
      if(limit >= this->_bid){
        this->_bid = limit;
        this->_bid_size = this->_chain_size(orders);
      }
      if(limit < this->_low_buy_limit)
        this->_low_buy_limit = limit;
    }
    else
    {
      if(limit <= this->_ask){
        this->_ask = limit;
        this->_ask_size = this->_chain_size(orders);
      }
      if(limit > this->_high_sell_limit)
        this->_high_sell_limit = limit;
    }
  }
  
  if(plccb) /* pre-completion callback*/
    plccb(id);
}

SOB_TEMPLATE
void SOB_CLASS::_insert_market_order(bool buy,
                                     size_type size,
                                     callback_type callback,
                                     id_type id)
{
  size_type rmndr = size;

  rmndr = buy ? this->_lift_offers(nullptr,id,size,callback)
              : this->_hit_bids(nullptr,id,size,callback);
  if(rmndr)
    throw liquidity_exception("market order couldn't fill");
}

SOB_TEMPLATE
void SOB_CLASS::_insert_stop_order(bool buy,
                                   plevel stop,
                                   size_type size,
                                   callback_type callback,
                                   id_type id)
{
  this->_insert_stop_order(buy, stop, nullptr, size, std::move(callback), id);
}

SOB_TEMPLATE
void SOB_CLASS::_insert_stop_order(bool buy,
                                   plevel stop,
                                   plevel limit,
                                   size_type size,
                                   callback_type callback,
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
  stop_bndl_type bndl = stop_bndl_type(buy,(void*)limit,size,callback);
  orders->insert(stop_chain_type::value_type(id,std::move(bndl)));
  /*
   * we maintain references to the most extreme stop prices so we can
   * avoid searching the entire array for triggered orders
   */
  if(buy && stop < this->_low_buy_stop)
    this->_low_buy_stop = stop;
  else if(!buy && stop > this->_high_sell_stop)
    this->_high_sell_stop = stop;  
}

SOB_TEMPLATE
template< bool BuyNotSell>
typename SOB_CLASS::market_depth_type 
SOB_CLASS::_market_depth(size_type depth) const
{
  plevel beg,end;
  market_depth_type md;

  /* from high to low */
  beg = BuyNotSell ? this->_bid
                   : (plevel)min(this->_ask + depth - 1, this->_end - 1);
  end = BuyNotSell ? (plevel)max(this->_beg, this->_bid - depth +1)
                   : this->_ask;

  for( ; beg >= end; --beg)
    if(!beg->first.empty())
      md.insert( market_depth_type::value_type(
                   this->_itop(beg), this->_chain_size(&beg->first)) );
  return md;
}

SOB_TEMPLATE
template< typename ChainTy>
size_type SOB_CLASS::_chain_size(ChainTy* chain) const
{ 
  ASSERT_VALID_CHAIN(ChainTy);
  size_type sz = 0;
  for(typename ChainTy::value_type& e : *chain)
    sz += e.second.first;
  return sz;
}


SOB_TEMPLATE
template< typename ChainTy, bool IsLimit>
bool SOB_CLASS::_pull_order(id_type id)
{
  plevel beg, end, hstop, lstop;  
  callback_type cb;
  ChainTy* c;
  id_type eid;
  bool buystop;
  
  //constexpr bool islim = std::is_same<ChainTy,limit_chain_type>::value;
  ASSERT_VALID_CHAIN(ChainTy);

  eid = 0;
  lstop = (plevel)min((plevel)min(this->_low_sell_stop,this->_low_buy_stop),
                      (plevel)min(this->_high_sell_stop,this->_high_buy_stop));
  hstop = (plevel)max((plevel)max(this->_low_sell_stop,this->_low_buy_stop),
                      (plevel)max(this->_high_sell_stop,this->_high_buy_stop));
  /* form low to high */
  beg = IsLimit ? this->_low_buy_limit : lstop;
  end = IsLimit ? this->_high_sell_limit : hstop; 
  
  for( ; beg <= end; ++beg)
  {
    c = IsLimit ? (ChainTy*)&beg->first : (ChainTy*)&beg->second;
    for(typename ChainTy::value_type& e : *c)
      if(e.first == id){
        /* match id and break so we can safely modify '*c' */
        eid = e.first; 
        break;
      }  
    if(eid){
      typename ChainTy::mapped_type bndl = c->at(eid);
      /* get the callback and, if stop order, its direction... before erasing */
      cb = this->_get_cb_from_bndl(bndl); 
      if(!IsLimit) 
        buystop = std::get<0>(bndl); 
      c->erase(eid); 
      /* adjust cache vals as necessary */
      if(IsLimit && c->empty())
        this->_adjust_limit_cache_vals(beg);
      else if(!IsLimit && buystop)     
          this->_adjust_stop_cache_vals<true>(beg,(stop_chain_type*)c);
      else if(!IsLimit && !buystop)
          this->_adjust_stop_cache_vals<false>(beg,(stop_chain_type*)c);     
      /* call back with cancel msg */
      cb(callback_msg::cancel,id,0,0);
      return true;
    }
  }
  return false;
}

SOB_TEMPLATE
void SOB_CLASS::_adjust_limit_cache_vals(plevel plev)
{  
  if(plev > this->_high_sell_limit)
    throw cache_value_error("can't remove limit higher than cached val");
  else if(plev == this->_high_sell_limit)
    --(this->_high_sell_limit); /*dont look for next valid plevel*/
  
  if(plev < this->_low_buy_limit)
    throw cache_value_error("can't remove limit lower than cached val");
  else if(plev == this->_low_buy_limit)
    ++(this->_low_buy_limit); /*dont look for next valid plevel*/   
}

SOB_TEMPLATE
template<bool BuyStop>
void SOB_CLASS::_adjust_stop_cache_vals(plevel plev,stop_chain_type* c)
{  
  stop_chain_type::const_iterator biter, eiter, riter;  
  
  biter = c->cbegin();
  eiter = c->cend();

  riter = find_if(biter, eiter, [](const stop_chain_type::value_type& v){
                                  return std::get<0>(v.second) == BuyStop;
                                } );
  if(riter == eiter){
    if(BuyStop)
    {
      if(plev > this->_high_buy_stop)
        throw cache_value_error("can't remove stop higher than cached val");
      else if(plev == this->_high_buy_stop)
        --(this->_high_buy_stop); /*dont look for next valid plevel*/ 
      
      if(plev < this->_low_buy_stop)
        throw cache_value_error("can't remove stop lower than cached val");
      else if(plev == this->_low_buy_stop)
        ++(this->_low_buy_stop); /*dont look for next valid plevel*/   
    }
    else
    {
      if(plev > this->_high_sell_stop)
        throw cache_value_error("can't remove stop higher than cached val");
      else if(plev == this->_high_sell_stop)
        --(this->_high_sell_stop); /*dont look for next valid plevel */   
      
      if(plev < this->_low_sell_stop)
        throw cache_value_error("can't remove stop lower than cached val");
      else if(plev == this->_low_sell_stop)
        ++(this->_low_sell_stop); /*dont look for next valid plevel*/       
    }    
  }    
}

SOB_TEMPLATE
template< bool BuyNotSell >
void SOB_CLASS::_dump_limits() const
{ 
  plevel beg,end;

  /* from high to low */
  beg = BuyNotSell ? this->_bid : this->_high_sell_limit;
  end = BuyNotSell ? this->_low_buy_limit : this->_ask;

  for( ; beg >= end; --beg)  
    if(!beg->first.empty())
    {
      std::cout<< this->_itop(beg);
      for(const limit_chain_type::value_type& e : beg->first)
        std::cout<< " <" << e.second.first << " #" << e.first << "> ";
      std::cout<< std::endl;
    } 
}

SOB_TEMPLATE
template< bool BuyNotSell >
void SOB_CLASS::_dump_stops() const
{ 
  plevel beg,end, plim;

  /* from high to low */
  beg = BuyNotSell ? this->_high_buy_stop : this->_high_sell_stop;
  end = BuyNotSell ? this->_low_buy_stop : this->_low_sell_stop;
  
  for( ; beg >= end; --beg) 
    if(!beg->second.empty())
    {
      std::cout<< this->_itop(beg);
      for(const stop_chain_type::value_type& e : beg->second){
        plim = (plevel)std::get<1>(e.second);
        std::cout<< " <" << (std::get<0>(e.second) ? "B " : "S ")
                 << std::to_string(std::get<2>(e.second)) << " @ "
                 << (plim ? std::to_string(this->_itop(plim)) : "MKT")
                 << " #" << std::to_string(e.first) << "> ";
      }
      std::cout<< std::endl;
    } 
}

SOB_TEMPLATE
void SOB_CLASS::dump_cached_plevels() const
{ /* DEBUG */
  std::cout<< "CACHED PLEVELS" << std::endl;
  std::cout<< "_high_sell_limit: "
         << std::to_string(this->_itop(this->_high_sell_limit)) << std::endl;
  std::cout<< "_high_buy_stop: "
         << std::to_string(this->_itop(this->_low_buy_stop)) << std::endl;
  std::cout<< "_low_buy_stop: "
         << std::to_string(this->_itop(this->_low_buy_stop)) << std::endl;
  std::cout<< "LAST: "
         << std::to_string(this->_itop(this->_last)) << std::endl;
  std::cout<< "_high_sell_stop: "
           << std::to_string(this->_itop(this->_high_sell_stop)) << std::endl;
  std::cout<< "_low_sell_stop: "
          << std::to_string(this->_itop(this->_low_sell_stop)) << std::endl;
  std::cout<< "_low_buy_limit: "
           << std::to_string(this->_itop(this->_low_buy_limit)) << std::endl;
}

SOB_TEMPLATE
typename SOB_CLASS::plevel SOB_CLASS::_ptoi(my_price_type price) const
{
  plevel plev;
  size_type incr_offset;

  incr_offset = round((price - this->_base) * tick_ratio::den/tick_ratio::num);
  plev = this->_beg + incr_offset;

  if(plev < this->_beg)
    throw std::range_error( "chain_pair_type* < _beg" );

  if(plev >= this->_end )
    throw std::range_error( "plevel >= _end" );

  return plev;
}

SOB_TEMPLATE 
typename SOB_CLASS::my_price_type SOB_CLASS::_itop(plevel plev) const
{
  price_type incr_offset;
  long long offset;

  if(plev < this->_beg)
    throw std::range_error( "plevel < _beg" );

  if(plev >= this->_end )
    throw std::range_error( "plevel >= _end" );

  offset = plev - this->_beg;
  incr_offset = (double)offset * tick_ratio::num / tick_ratio::den;

  return this->_base + my_price_type(incr_offset);
}

SOB_TEMPLATE
size_type SOB_CLASS::_incrs_in_range(my_price_type lprice, my_price_type hprice)
{
  my_price_type h(this->_round_to_incr(hprice));
  my_price_type l(this->_round_to_incr(lprice));
  size_type i = round((double)(h-l)*tick_ratio::den/tick_ratio::num);
  
  if(lprice < 0 || hprice < 0)
    throw invalid_parameters("price/min/max values can not be < 0"); 
  
  if(i < 0)
    throw invalid_parameters("invalid price/min/max price value(s)");
    
  return (size_type)i;
}

SOB_TEMPLATE
size_type SOB_CLASS::_generate_and_check_total_incr()
{
  size_type i;
  
  if(this->_lower_incr < 1 || this->_upper_incr < 1)  
    throw invalid_parameters("parameters don't generate enough increments"); 
  
  i = this->_lower_incr + this->_upper_incr + 1;
  
  if(i > max_ticks)
    throw invalid_parameters("tick range requested would exceed MaxMemory");
  
  return i;
}

SOB_TEMPLATE 
SOB_CLASS::SimpleOrderbook(my_price_type price, 
                           my_price_type min, 
                           my_price_type max)
  :
  _bid_size(0),
  _ask_size(0),
  _last_size(0), 
  _lower_incr(this->_incrs_in_range(min,price)),
  _upper_incr(this->_incrs_in_range(price,max)),
  _total_incr(this->_generate_and_check_total_incr()),
  _base( min ),
  _book( _total_incr + 1), /* pad the beg sid so we can go past w/o seg fault */
  _beg( &(*_book.begin()) + 1 ),
  _end( &(*_book.end())), 
  _last( this->_beg + _lower_incr ), 
  _bid( &(*(this->_beg-1)) ),
  _ask( &(*(this->_end)) ),
  _low_buy_limit( &(*this->_last) ),
  _high_sell_limit( &(*this->_last) ),
  _low_buy_stop( &(*(this->_end-1)) ),
  _high_buy_stop( &(*(this->_end-1)) ),
  _low_sell_stop( &(*this->_beg) ),
  _high_sell_stop( &(*this->_beg) ),
  _total_volume(0),
  _last_id(0),
  _market_makers(), 
  _is_dirty(false),
  _calling_back(false),
  _deferred_callback_queue(),
  _order_queue(),
  _t_and_s(),
  _t_and_s_max_sz(1000),
  _t_and_s_full(false),
  _queue_mutex(),
  _in_signal(),
  _background_thread1(std::bind(&SOB_CLASS::_exec_order_queue,this))
  {       
    this->_t_and_s.reserve(this->_t_and_s_max_sz);   
    this->_background_thread1.detach();
    std::cout<< "+ SimpleOrderbook(" << typeid(*this).name() << ") Created\n";
  }

SOB_TEMPLATE 
SOB_CLASS::~SimpleOrderbook()
{
  std::cout<< "- SimpleOrderbook Destroyed\n";
}

SOB_TEMPLATE
void SOB_CLASS::add_market_makers(market_makers_type&& mms)
{ /* start and steal the market_maker smart_pointers */
  for(pMarketMaker& mm : mms){
    mm->start(this, this->_itop(this->_last), tick_size);
    this->_market_makers.push_back(std::move(mm));
  }
}

SOB_TEMPLATE
void SOB_CLASS::add_market_maker(MarketMaker&& mm)
{ /* create market_maker smart_pointer, start and push */
  pMarketMaker pmm = mm._move_to_new();
  pmm->start(this, this->_itop(this->_last), tick_size);
  this->_market_makers.push_back(std::move(pmm)); 
}

SOB_TEMPLATE
id_type SOB_CLASS::insert_limit_order(bool buy,
                                      price_type limit,
                                      size_type size,
                                      callback_type callback,
                                      post_exec_callback_type plccb) 
{
  plevel plev;
  
  if(size <= 0)
    throw invalid_order("invalid order size");  
  /*
   * if we want to do this, need to allow MMs to insert ...
   * 
  if(this->_market_makers.empty())
    throw invalid_state("orderbook has no market makers"); */   
  try{
    plev = this->_ptoi(limit);  
  }catch(std::range_error){
    throw invalid_order("invalid limit price");
  }    
 
  return this->_push_order_and_wait(order_type::limit, buy, plev, nullptr,
                                    size, callback, plccb);  
}

SOB_TEMPLATE
id_type SOB_CLASS::insert_market_order(bool buy,
                                       size_type size,
                                       callback_type callback)
{  
  if(size <= 0)
    throw invalid_order("invalid order size");
  
  if(this->_market_makers.empty())
    throw invalid_state("orderbook has no market makers");  
  
  return this->_push_order_and_wait(order_type::market, buy, nullptr, nullptr,
                                    size, callback); 
}

SOB_TEMPLATE
id_type SOB_CLASS::insert_stop_order(bool buy,
                                     price_type stop,
                                     size_type size,
                                     callback_type callback)
{
  return this->insert_stop_order(buy,stop,0,size,callback);
}

SOB_TEMPLATE
id_type SOB_CLASS::insert_stop_order(bool buy,
                                     price_type stop,
                                     price_type limit,
                                     size_type size,
                                     callback_type callback)
{
  plevel plimit, pstop;
  order_type oty;

  if(size <= 0)
    throw invalid_order("invalid order size");
  
  if(this->_market_makers.empty())
    throw invalid_state("orderbook has no market makers");

  try{
    plimit = limit ? this->_ptoi(limit) : nullptr;
    pstop = this->_ptoi(stop);     
  }catch(std::range_error){
    throw invalid_order("invalid price");
  }  

  oty = limit ? order_type::stop_limit : order_type::stop;
  return this->_push_order_and_wait( oty, buy, plimit, pstop, size, callback);  
}

SOB_TEMPLATE
bool SOB_CLASS::pull_order(id_type id, bool search_limits_first)
{   
  if(search_limits_first)
    return this->_pull_order<limit_chain_type>(id) ||
           this->_pull_order<stop_chain_type>(id);
  else
    return this->_pull_order<stop_chain_type>(id) ||
           this->_pull_order<limit_chain_type>(id);    
}


SOB_TEMPLATE
id_type 
SOB_CLASS::replace_with_limit_order(id_type id,
                                    bool buy,
                                    price_type limit,
                                    size_type size,
                                    callback_type callback,
                                    post_exec_callback_type plccb)
{
  id_type id_new = 0;
  
  if(this->pull_order(id))
    id_new = this->insert_limit_order(buy,limit,size,callback,plccb);
  
  return id_new;
}

SOB_TEMPLATE
id_type SOB_CLASS::replace_with_market_order(id_type id,
                                             bool buy,
                                             size_type size,
                                             callback_type callback)
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
                                           callback_type callback)
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
                                           callback_type callback)
{
  id_type id_new = 0;
  
  if(this->pull_order(id))
    id_new = this->insert_stop_order(buy,stop,limit,size,callback);
  
  return id_new;
}

};
};
