// example_code.cpp

#include "simpleorderbook.hpp"

void 
execution_callback(sob::callback_msg msg, 
                   sob::id_type id1,
                   sob::id_type id2,
                   double price,
                   size_t size);

void 
insert_orders(sob::FullInterface *orderbook);

void 
insert_advanced_orders(sob::FullInterface *orderbook);

void
print_inside_market(sob::QueryInterface *orderbook);

int
main(int argc, char* argv[])
{
    using namespace sob;

    /* 
     * First, we need to create a factory proxy. To support different 
     * orderbook types and different constructor types, in a single factory, 
     * we provide different factory interfaces(proxies).
     *
     * The following will be used for managing orderbooks of (implementation) type:
     *     SimpleOrderbook::SimpleOrderbookImpl< std::ratio<1,4> >
     *
     * - uses the default factory 'create' function via (double,double) constructor
     * - with .25 price intervals       
     * 
     * Proxies (implicitly) restrict default construction and assignment
     */         
    SimpleOrderbook::FactoryProxy<> qt_def_proxy = 
        SimpleOrderbook::BuildFactoryProxy<quarter_tick>();            

   /*
    * This approach(assuming we use the default FactoryProxy) 
    * provides factory interfaces - for each and ANY type of 
    * orderbook - all of the same type; allowing for:
    */           
    std::map<std::string, SimpleOrderbook::FactoryProxy<>> 
    my_factory_proxies = { 
        {"QT", qt_def_proxy},
        {"TT", SimpleOrderbook::BuildFactoryProxy<tenth_tick>()},
        {"HT", SimpleOrderbook::BuildFactoryProxy<std::ratio<1,2>>()}
    };

    /*  
     * Use the factory proxy to create an orderbook that operates 
     * between .25 and 100.00 in .25 increments, returning a pointer
     * to its full interface 
     *
     * NOTE: .create() is built to throw: logic and runtime errors (handle accordingly)
     */ 
    FullInterface *orderbook = my_factory_proxies.at("QT").create(.25, 100.00);                  
    if( !orderbook ){
        // error (this *shouldn't* happen)
        return 1;
    }

    /* 
     * IMPORTANT: internally, each orderbook is managed by a 'resource manager' 
     * behind each proxy AND a global one for ALL proxies/orderbooks. This 
     * allows for the use of that type's proxy member functions OR the global
     * static methods of the SimpleOrderbook container class. (see below)
     */

    /* check if orderbook is being managed by ANY proxy */
    if( !SimpleOrderbook::IsManaged(orderbook) )
    {
        /* get orderbooks being managed by ALL proxies */
        std::vector<FullInterface*> actives = SimpleOrderbook::GetAll();

        std::cerr<< "error: active orderbooks for ALL proxies" << std::endl;
        for( FullInterface *i : actives ){
            std::cerr<< std::hex << reinterpret_cast<void*>(i) << std::endl;
        }
        return 1;
    }       

    /* check if orderbook is being managed by THIS proxy */
    if( !my_factory_proxies.at("QT").is_managed(orderbook) )
    {
        /* get all orderbooks being managed by THIS proxy */
        std::vector<FullInterface*> actives = my_factory_proxies.at("QT").get_all();

        std::cerr<< "error: active orderbooks for 'QT' proxy" << std::endl;
        for( FullInterface *i : actives ){
            std::cerr<< std::hex << reinterpret_cast<void*>(i) << std::endl;
        }
        return 1;
    }    

    /* use the full interface (defined below) */
    insert_orders(orderbook);            

    /* use the query interface (defined below) */
    print_inside_market(orderbook);

    /* increase the size of the orderbook */
    dynamic_cast<ManagementInterface*>(orderbook)->grow_book_above(150);

    if( orderbook->min_price() == .25 
        && orderbook->max_price() == 150.00
        && orderbook->tick_size() == .25
        && orderbook->price_to_tick(150.12) == 150.00
        && orderbook->price_to_tick(150.13) == 150.25
        && orderbook->is_valid_price(150.13) == false
        && orderbook->ticks_in_range(.25, 150) == 599 
        && orderbook->tick_memory_required(.25, 150) == orderbook->tick_memory_required() )
    {
        std::cout<< "good orderbook" << std::endl;
    }

    /* use advanced orders (IN DEVELOPEMENT) */
    insert_advanced_orders(orderbook);

    /* 
     * WHEN DONE...
     */
  
    /* use the proxy to destroy the orderbook it created */
    my_factory_proxies.at("QT").destroy(orderbook);

    /* (or) use the global version to destroy ANY orderbook 
       NOTE: orderbook should only be destroyed once (no-op in this case) */
    SimpleOrderbook::Destroy(orderbook);

    /* use the proxy to destroy all the orderbooks it created */
    my_factory_proxies.at("QT").destroy_all();

    /* (or) use the global version to destroy ALL orderbooks */
    SimpleOrderbook::DestroyAll();

    //...
    
    return 0;
}   

void 
execution_callback(sob::callback_msg msg, 
                   sob::id_type id1,
                   sob::id_type id2,
                   double price,
                   size_t size)
{
    std::cout<< msg << " " << id1 << " " << id2 << " " 
             << price << " " << size << std::endl;
    // define
}

void 
insert_orders(sob::FullInterface *orderbook)
{
    /* buy 50 @ 49.75 or better */
    sob::id_type id1 = orderbook->insert_limit_order(true, 49.75, 50, execution_callback);
    /* sell 10 @ market */
    sob::id_type id2 = orderbook->insert_market_order(false, 10, execution_callback);

    /* pull orders */
    orderbook->pull_order(id1);
    orderbook->pull_order(id2);}

void 
insert_advanced_orders(sob::FullInterface *orderbook)
{
    /* create a OCO (one-cancels-other) buy-limit/sell-limit order */

    /* first create an AdvancedOrderTicket */
    auto aot = sob::AdvancedOrderTicketOCO::build_limit(false, 50.00, 100, 
                    sob::condition_trigger::fill_partial);

    /* then insert it to the standard interface 
       NOTE: it will call back when the condition is triggered,
             the new order id will be in field 'id2' */
    sob::id_type id3 = orderbook->insert_limit_order(true, 49.50, 100, execution_callback, aot);

    /* if either order fills the other is canceled */
    orderbook->insert_market_order(false, 50);
}

void
print_inside_market(sob::QueryInterface *orderbook)
{
    std::cout<< "BID: " << orderbook->bid_size() 
             << " @ " << orderbook->bid_price() << std::endl;
    std::cout<< "ASK: " << orderbook->ask_size() 
             << " @ " << orderbook->ask_price() << std::endl;
    std::cout<< "LAST: " << orderbook->last_size() 
             << " @ " << orderbook->last_price() << std::endl;
}
