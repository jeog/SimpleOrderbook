#
# Copyright (C) 2017 Jonathon Ogden < jeog.dev@gmail.com >
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNE A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see http://www.gnu.org/licenses. 
#

import simpleorderbook as sob
import os

TICK_TYPE = sob.SOB_QUARTER_TICK
P_TO_T = lambda p : sob.price_to_tick(TICK_TYPE, p)
BOOK_MIN = P_TO_T(1)
BOOK_MAX = P_TO_T(100)
BOOK_MID = P_TO_T((BOOK_MIN + BOOK_MAX)/2)
BOOK_INCR = sob.tick_size(TICK_TYPE)
SZ = 100
SZ2 = 500
NTICKS = 10

def check_val(val1, val2, tag):
    if val1 != val2:
        raise Exception("*** ERROR - %s - (%i,%i) ***" % (tag,val1,val2))
                
def test_AON():
    aot = sob.AdvancedOrderTicketAON.build()
    check_val(aot.condition, sob.CONDITION_AON, "AON.condition")
    check_val(aot.trigger, sob.TRIGGER_NONE, "AON.trigger")
    
    book = sob.SimpleOrderbook(TICK_TYPE, BOOK_MIN, BOOK_MAX)
    
    def cb(msg,id1, id2, price, size):
        pass #print("AON callback: ", msg, id1, id2, price, size) 

    o1 = book.sell_limit(BOOK_MID + BOOK_INCR, SZ, cb)
    o2 = book.buy_limit(BOOK_MID + BOOK_INCR, SZ, cb)
    o3 = book.sell_limit(BOOK_MID + BOOK_INCR, SZ, cb)
    o4 = book.sell_limit(BOOK_MID + BOOK_INCR*2, SZ, cb)
    o5 = book.buy_limit(BOOK_MID + BOOK_INCR*2, SZ*2, cb, advanced = aot)

    if book.bid_size() != 0:
        raise Exception("*** ERROR bid size(%i) != %i ***" % (book.bid_size(), 0))

    if book.ask_size() != 0:
        raise Exception("*** ERROR ask size(%i) != %i ***" % (book.ask_size(), 0))

    if book.volume() != SZ*3:
        raise Exception("*** ERROR volume(%i) != %i ***" % (book.volume(), SZ*3))

    #book.dump_buy_limits()
    #book.dump_sell_limits()
    #book.dump_aon_buy_limits()
    #book.dump_aon_sell_limits()

    o6 = book.sell_limit(BOOK_MID + BOOK_INCR, SZ, cb)
    o7 = book.sell_limit(BOOK_MID + BOOK_INCR*2, SZ, cb)
    o8 = book.buy_limit(BOOK_MID + BOOK_INCR*2, int(SZ *1.5), cb, advanced=aot)

    #book.dump_limits()
    #book.dump_stops()
    #book.dump_aon_limits()

    if book.volume() != SZ * 4.5:
        raise Exception("*** ERROR volume(%i) != %i ***" % (book.volume(), SZ*4.5))

    oi = book.get_order_info(o7)
    if oi.limit != BOOK_MID + BOOK_INCR*2:
        raise Exception("*** ERROR order_info.limit(%f) != %f ***" % (oi.limit, BOOK_MID + BOOK_INCR*2))
    if oi.size != SZ*.5:
        raise Exception("*** ERROR order_info.size(%i) != %i ***" % (oi.size, SZ*.5))

    if not book.pull_order(o7):
        raise Exception("*** ERROR failed to remove order 'o7', id: %i" % o7)

    oi = book.get_order_info(o8)
    if oi:
        raise Exception("*** ERROR order_info should be None ***")
 
    if book.pull_order(o8):
        raise Exception("*** ERROR successfully to removed order 'o8', id: %i" % o8)
  
    #book.dump_aon_buy_limits()
    #book.dump_aon_sell_limits()

    book = sob.SimpleOrderbook(TICK_TYPE, BOOK_MIN, BOOK_MAX)
    book.sell_limit( BOOK_MID + BOOK_INCR, SZ, cb, aot)
    book.sell_limit( BOOK_MID + BOOK_INCR*2, SZ, cb, aot)
 
    tabs = book.total_aon_bid_size()
    taas = book.total_aon_ask_size()
    tas = book.total_aon_size()
    if tabs != 0:
        raise Exception("*** ERROR total_aon_bid_size(%i) != %i ***" % (tabs,0))

    if taas != 2*SZ:
        raise Exception("*** ERROR total_aon_ask_size(%i) != %i ***" % (taas, 2*SZ))

    if tas != 2*SZ:
        raise Exception("*** ERROR total_aon_size(%i) != %i ***" % (tas, 2*SZ))

    #book.dump_aon_buy_limits()
    #book.dump_aon_sell_limits()
    #book.dump_aon_limits()

    o9 = book.buy_limit( BOOK_MIN, 1, advanced=aot)
    md = book.aon_market_depth()
 
    if len(md) != 3:
        raise Exception("*** ERROR len(aon_market_depth)(%i) != %i ***" % (len(md),3))

    if BOOK_MIN not in md:
        raise Exception("*** ERROR level(%f) not in aon_market_depth) ***" % BOOK_MIN)

    buysz = md[BOOK_MIN][0]
    if buysz != 1:
        raise Exception("*** ERROR aon buy size @ %f(%i) != %i ***" % (BOOK_MIN, buysz, 1))
    
    o10 = book.replace_with_buy_limit(o9, BOOK_MIN + BOOK_INCR, SZ, cb)
    if not o10:
        raise Exception("*** ERROR failed to replace order %i ***" % o9)

    if not book.pull_order(o10):
        raise Exception("*** ERROR failed to pull order %i ***" % o10)

    md = book.aon_market_depth()
    if len(md) != 2:
        raise Exception("*** ERROR len(aon_market_depth)(%i) != %i ***" % (len(md),2))

    if BOOK_MID + BOOK_INCR not in md:
        raise Exception("*** ERROR level(%f) not in aon_market_depth) ***" % BOOK_MID+BOOK_INCR)

    if BOOK_MID + BOOK_INCR*2 not in md:
        raise Exception("*** ERROR level(%f) not in aon_market_depth) ***" % BOOK_MID+BOOK_INCR*2)

    book.buy_limit( BOOK_MID + BOOK_INCR*2, int(SZ*2.5), cb)

    bs = book.bid_size()
    tbs = book.total_bid_size()
    ts = book.total_size()
    if bs != SZ/2:
        raise Exception("*** ERROR bid_size(%i) != %i ***" % (bs,SZ/2))

    if tbs != SZ/2:
        raise Exception("*** ERROR tota_bid_size(%i) != %i ***" % (tbs, SZ/2))

    if ts != SZ/2:
        raise Exception("*** ERROR total_size(%i) != %i ***" % (ts, SZ/2))

    tabs = book.total_aon_bid_size()
    tas = book.total_aon_size()
    if tabs != 0:
        raise Exception("*** ERROR total_aon_bid_size(%i) != %i ***" % (tabs,0))

    if tas != 0:
        raise Exception("*** ERROR total_aon_size(%i) != %i ***" % (tas, 0))
   
    md = book.aon_market_depth()
    if len(md) != 0:
        raise Exception("*** ERROR len(aon_market_depth)(%i) != %i ***" % (len(md),0))

def test_OCO():
    DEF_TRIGGER = sob.TRIGGER_FILL_PARTIAL
    def check_aot(aot, cond, trigger, is_buy, sz, limit, stop, tag):
        check_val(aot.condition, cond, "OCO - " + tag + " - .condition")
        check_val(aot.trigger, trigger, "OCO - " + tag + " - .trigger")  
        check_val(aot.is_buy, is_buy, "OCO - " + tag + " - .is_buy")
        check_val(aot.size, sz, "OCO - " + tag + " - .size")
        check_val(aot.limit, limit, "OCO - " + tag + " - .limit")
        check_val(aot.stop, stop, "OCO - " + tag + " - .stop")            
    
    aot_limit = sob.AdvancedOrderTicketOCO.build_limit(True, BOOK_MID, SZ)
    check_aot(aot_limit, sob.CONDITION_OCO, DEF_TRIGGER, True, 
              SZ, BOOK_MID, 0, 'lmit AOT')
    
    aot_stop = sob.AdvancedOrderTicketOCO.build_stop(False, BOOK_MIN, SZ*2)
    check_aot(aot_stop, sob.CONDITION_OCO, DEF_TRIGGER, False, SZ*2, 0, 
              BOOK_MIN, 'stop AOT')
    
    aot_stop_limit = sob.AdvancedOrderTicketOCO.build_stop_limit(True, BOOK_MID, 
                                                                 BOOK_MAX, 1)
    check_aot(aot_stop_limit, sob.CONDITION_OCO, DEF_TRIGGER, True,1, 
              BOOK_MAX, BOOK_MID, 'stop_limit AOT') 
    
    book = sob.SimpleOrderbook(TICK_TYPE, BOOK_MIN, BOOK_MAX)
    o1 = book.sell_limit(BOOK_MID + BOOK_INCR, SZ2, advanced=aot_limit)
    o2 = book.sell_stop_limit(BOOK_MID - BOOK_INCR, BOOK_MIN, SZ2, 
                              advanced=aot_stop_limit)
    #                           (L,o1.a,SZ2)
    #  (L,o1.b,SZ) (S,o2.b,1)   (SL,o2.a,SZ2)
    o3 = book.buy_market(SZ)    
    #                           (L,o1.a,SZ2-SZ)

    if book.volume() != SZ+1:
        raise Exception("*** ERROR vol(%i) != %i ***" % (book.volume(), SZ+1))
    md = book.market_depth()
    if len(md) != 1:
        raise Exception("*** ERROR length of market_depth dict != %i ***" % 1)
    x = md[BOOK_MID+BOOK_INCR]
    if not x:
        raise Exception("*** ERROR invalid market_depth elem @ %f ***" % 
                        BOOK_MID+BOOK_INCR)
    if x[0] != SZ2 - SZ - 1:
        raise Exception("*** ERROR reaming size(%i) != %i ***" % (x[0],SZ2-SZ-1))
        
    
    
def test_OTO():
    DEF_TRIGGER = sob.TRIGGER_FILL_PARTIAL
    
    def check_aot(aot, cond, trigger, is_buy, sz, limit, stop, tag):
        check_val(aot.condition, cond, "OTO - " + tag + " - .condition")
        check_val(aot.trigger, trigger, "OTO - " + tag + " - .trigger") 
        check_val(aot.is_buy, is_buy, "OTO - " + tag + " - .is_buy")
        check_val(aot.size, sz, "OTO - " + tag + " - .size")
        check_val(aot.limit, limit, "OTO - " + tag + " - .limit")
        check_val(aot.stop, stop, "OTO - " + tag + " - .stop")              
            
    aot_limit = sob.AdvancedOrderTicketOTO.build_limit(True, BOOK_MID, SZ)
    check_aot(aot_limit, sob.CONDITION_OTO, DEF_TRIGGER, True, SZ, BOOK_MID, 
              0 , 'limit AOT')
    
    aot_stop = sob.AdvancedOrderTicketOTO.build_stop(False, BOOK_MIN, SZ*2)
    check_aot(aot_stop, sob.CONDITION_OTO, DEF_TRIGGER, False, SZ*2, 0, 
              BOOK_MIN, 'stop AOT')
    
    aot_stop_limit = sob.AdvancedOrderTicketOTO.build_stop_limit(True, BOOK_MID, 
                                                                 BOOK_MAX, 1)
    check_aot(aot_stop_limit, sob.CONDITION_OTO, DEF_TRIGGER,True,1, BOOK_MAX, 
              BOOK_MID, 'stop_limit AOT') 
    
    aot_market = sob.AdvancedOrderTicketOTO.build_market(False, SZ*100);
    check_aot(aot_market, sob.CONDITION_OTO, DEF_TRIGGER,False, SZ*100, 0, 0, 
              'market AOT')

    book = sob.SimpleOrderbook(TICK_TYPE, BOOK_MIN, BOOK_MAX)
    
    o2 = 0
    def stop_cb(msg,id1, id2, price, size):       
        nonlocal o2
        if msg == sob.MSG_TRIGGER_OTO:
            o2 = id2
        
    o1 = book.sell_limit(BOOK_MID + BOOK_INCR, SZ2, advanced=aot_limit)
    o2 = book.buy_stop_limit(BOOK_MID + BOOK_INCR, BOOK_MAX, SZ, 
                              callback=stop_cb, advanced=aot_stop)
    #                           (L,o1.a,SZ2)
    #  (SL,o2.a,SZ)
    o3 = book.buy_market(SZ)   
    #                           (L,o1.a,SZ2-SZ-SZ)
    #  (L,o1.b,SZ) 
    #                           (SL,o2.b,SZ*2)

    if book.volume() != SZ+SZ:
        raise Exception("*** ERROR vol(%i) != %i ***" % (book.volume(), SZ+SZ))
    md = book.market_depth()
    if len(md) != 2:
        raise Exception("*** ERROR length of market_depth dict != %i ***" % 2)
    xl = md[BOOK_MID]
    if not xl:
        raise Exception("*** ERROR invalid market_depth elem @ %f ***" % BOOK_MID)
    if xl[0] != SZ:
        raise Exception("*** ERROR reaming size @ %f (%i) != %i ***" % 
                        (BOOK_MID, xl[0],SZ))
    xs = md[BOOK_MID + BOOK_INCR]
    if not xs:
        raise Exception("*** ERROR invalid market_depth elem @ %f ***" % 
                        BOOK_MID + BOOK_INCR)
    if xs[0] != SZ2-SZ-SZ:
        raise Exception("*** ERROR reaming size @ %f (%i) != %i ***" % 
                        (BOOK_MID + BOOK_INCR, xs[0],SZ2-SZ-SZ))        
    if not book.pull_order(o2):
        raise Exception("*** ERROR failed to remove OTO'd stop, id: %i" % o2)


def test_FOK():
    DEF_TRIGGER = sob.TRIGGER_FILL_FULL
    
    def check_aot(aot, cond, trigger, tag):              
        check_val(aot.condition, cond, "FOK - " + tag + " - .condition")
        check_val(aot.trigger, trigger, "FOK - " + tag + " - .trigger") 
            
    aot1 = sob.AdvancedOrderTicketFOK.build()
    check_aot(aot1, sob.CONDITION_FOK, DEF_TRIGGER, 'fill-full aot')
    
    aot2 = sob.AdvancedOrderTicketFOK.build( sob.TRIGGER_FILL_PARTIAL)
    check_aot(aot2, sob.CONDITION_FOK, sob.TRIGGER_FILL_PARTIAL, 'fill-partial aot')

    book = sob.SimpleOrderbook(TICK_TYPE, BOOK_MIN, BOOK_MAX)
    book.sell_limit(BOOK_MID, SZ)
    book.buy_limit(BOOK_MID, SZ*2, advanced=aot1)
    md = book.market_depth()
    if book.volume() or md[BOOK_MID][0] != SZ or md[BOOK_MID][1] != sob.SIDE_ASK :
        raise Exception("*** ERROR fill-full FOK failed ***")
    o1 = book.buy_limit(BOOK_MID, SZ*2, advanced=aot2)
    if book.volume() != SZ:
        raise Exception("*** ERROR vol(%i) != %i ***" % (book.volume(), SZ))
    md = book.market_depth()
    if not md[BOOK_MID]:
         raise Exception("*** ERROR invalid market_depth elem @ %f ***" % BOOK_MID)       
    if md[BOOK_MID][0] != SZ:
        raise Exception("*** ERROR reaming size @ %f (%i) != %i ***" % 
                        (BOOK_MID, md[BOOK_MID][0],SZ))    
    if md[BOOK_MID][1] != sob.SIDE_BID:
        raise Exception("*** ERROR remaing limit not on correct side of market")


def test_BRACKET():
    DEF_TRIGGER = sob.TRIGGER_FILL_PARTIAL
    AOT = sob.AdvancedOrderTicketBRACKET
    
    def check_aot(aot, cond, trigger, is_buy,loss_stop, loss_limit, 
                  target_limit, tag):
        check_val(aot.condition, cond, "BRACKET - " + tag + " - .condition")
        check_val(aot.trigger, trigger, "BRACKET - " + tag + " - .trigger") 
        check_val(aot.is_buy, is_buy, "BRACKET - " + tag + " - .is_buy")     
        check_val(aot.loss_stop, loss_stop, "BRACKET - " + tag + " - .loss_stop")  
        check_val(aot.loss_limit, loss_limit, "BRACKET - " + tag + " - .loss_limit")                     
        check_val(aot.target_limit, target_limit, 
                  "BRACKET - " + tag + " - .target_limit")
            
    aot_ssl = AOT.build_sell_stop_limit(BOOK_MID, BOOK_MIN, BOOK_MAX,
                                        sob.TRIGGER_FILL_FULL)        
    check_aot(aot_ssl, sob.CONDITION_BRACKET, sob.TRIGGER_FILL_FULL, False, BOOK_MID, 
              BOOK_MIN, BOOK_MAX, 'sell_stop_limit fill-partial AOT')

    aot_bsl = AOT.build_buy_stop_limit(BOOK_MID, BOOK_MAX, BOOK_MIN,
                                       sob.TRIGGER_FILL_FULL)        
    check_aot(aot_bsl, sob.CONDITION_BRACKET, sob.TRIGGER_FILL_FULL, True,  
              BOOK_MID, BOOK_MAX, BOOK_MIN, 'buy_stop_limit fill-full AOT')    

    aot_ss = AOT.build_sell_stop(BOOK_MID, BOOK_MAX, sob.TRIGGER_FILL_FULL)        
    check_aot(aot_ss, sob.CONDITION_BRACKET, sob.TRIGGER_FILL_FULL, False, 
              BOOK_MID, 0, BOOK_MAX, 'sell_stop fill-full AOT') 

    aot_bs = AOT.build_buy_stop(BOOK_MID, BOOK_MIN, sob.TRIGGER_FILL_FULL)       
    check_aot(aot_bs, sob.CONDITION_BRACKET, sob.TRIGGER_FILL_FULL, True, 
              BOOK_MID, 0, BOOK_MIN, 'buy_stop fill-partial AOT') 
    
    o1 = 0
    def stop_cb(msg, id1, id2, price, size):
        nonlocal o1   
        if msg == sob.MSG_TRIGGER_BRACKET_OPEN:         
            o1 = id2
            
    book = sob.SimpleOrderbook(TICK_TYPE, BOOK_MIN, BOOK_MAX)
    o1 = book.buy_limit(BOOK_MID, SZ, callback=stop_cb, advanced=aot_ssl)       
    oi = book.get_order_info(o1)    
    if not oi.advanced or oi.advanced.condition != sob.CONDITION_BRACKET:
        raise Exception("*** ERROR (1a) invalid OrderInfo.advanced")          
    book.sell_limit(BOOK_MID, SZ)
    #                       (L, o1.b.1, SZ)
    #  (SL,o1.b.2,SZ)      
    md = book.market_depth()
    if not md[BOOK_MAX]:
         raise Exception("*** ERROR invalid limit @ %f ***" % BOOK_MAX)       
    if md[BOOK_MAX][0] != SZ:
        raise Exception("*** ERROR reaming size @ %f (%i) != %i ***" % 
                        (BOOK_MAX, md[BOOK_MAX][0],SZ))    
    if md[BOOK_MAX][1] != sob.SIDE_ASK:
        raise Exception("*** ERROR bracket target not on correct side of market")
    
    oi = book.get_order_info(o1)    
    if not oi.advanced or oi.advanced.condition != sob.CONDITION_BRACKET_ACTIVE:
        raise Exception("*** ERROR (1b) invalid OrderInfo.advanced") 
    
    book.buy_limit(BOOK_MAX, SZ)
    md = book.market_depth()
    if md:
        raise Exception("*** ERROR orders still exists in book ***")
  
    o1 = book.sell_limit(BOOK_MID, SZ, callback=stop_cb, advanced=aot_bs)       
    oi = book.get_order_info(o1)    
    if not oi.advanced or oi.advanced.condition != sob.CONDITION_BRACKET:
        raise Exception("*** ERROR (2a) invalid OrderInfo.advanced")  
    book.buy_limit(BOOK_MID, SZ)
    md = book.market_depth()
    if not md[BOOK_MIN]:
         raise Exception("*** ERROR invalid limit @ %f ***" % BOOK_MIN)       
    #if md[BOOK_MIN][0] != 100*SZ:
    #    raise Exception("*** ERROR reaming size @ %f (%i) != %i ***" % 
    #                    (BOOK_MIN, md[BOOK_MIN][0],100*SZ))    
    if md[BOOK_MIN][1] != sob.SIDE_BID:
        raise Exception("*** ERROR bracket target not on correct side of market")
    
    oi = book.get_order_info(o1)
    if not oi.advanced or oi.advanced.condition != sob.CONDITION_BRACKET_ACTIVE:
        raise Exception("*** ERROR (2b) invalid OrderInfo.advanced") 
    
    book.sell_limit(BOOK_MIN, SZ)
    md = book.market_depth()
    #if md[BOOK_MIN][0] != 99*SZ:
    #    raise Exception("*** ERROR orders still exists in book ***")  
        
    null_fd = os.open(os.devnull, os.O_RDWR)
    err_fd = os.dup(2)
    os.dup2(null_fd, 2)       
    try:
        book.buy_market(1) # check nothing above        
    except:
        return
    finally:
        os.dup2(err_fd, 2)
        os.close(null_fd)
    
    raise Exception("*** ERROR sell orders were still active ***")
    
    
def test_TrailingStop():  
    def check_aot(aot, cond, trigger, nticks, tag):              
        check_val(aot.condition, cond, "TrailingStop - " + tag + " - .condition")
        check_val(aot.trigger, trigger, "TrailingStop - " + tag + " - .trigger")
        check_val(aot.nticks, nticks, "TrailingStop - " + tag + " - .nticks")  
            
    aot1 = sob.AdvancedOrderTicketTrailingStop.build(NTICKS, sob.TRIGGER_FILL_FULL)
    check_aot(aot1, sob.CONDITION_TRAILING_STOP, sob.TRIGGER_FILL_FULL, NTICKS, '')

    o1 = 0
    def stop_cb(msg, id1, id2, price, size):
        nonlocal o1   
        if msg == sob.MSG_TRIGGER_TRAILING_STOP_OPEN:         
            o1 = id2
        
    book = sob.SimpleOrderbook(TICK_TYPE, BOOK_MIN, BOOK_MAX)
    o1 = book.buy_limit(BOOK_MID, SZ, callback=stop_cb, advanced=aot1)         
    oi = book.get_order_info(o1)
    if oi.limit != BOOK_MID or oi.order_type != sob.ORDER_TYPE_LIMIT:
        raise Exception("*** ERROR (!) invalid OrderInfo")    
    if not oi.advanced or oi.advanced.nticks != NTICKS \
        or oi.advanced.condition != sob.CONDITION_TRAILING_STOP:
        raise Exception("*** ERROR (1) invalid OrderInfo.advanced") 
        
    book.sell_market(SZ)
       
    oi = book.get_order_info(o1)
    if oi.limit != 0 or oi.stop != (BOOK_MID - (NTICKS*BOOK_INCR)) \
        or oi.order_type != sob.ORDER_TYPE_STOP:
        raise Exception("*** ERROR (2) invalid OrderInfo")    
    if not oi.advanced or oi.advanced.condition != sob.CONDITION_TRAILING_STOP_ACTIVE:
        raise Exception("*** ERROR (2) invalid OrderInfo.advanced") 
    
    book.sell_limit(BOOK_MID + 10*BOOK_INCR, SZ)
    book.buy_market(SZ)
 
    oi = book.get_order_info(o1)
    if oi.stop != BOOK_MID or oi.order_type != sob.ORDER_TYPE_STOP:
        raise Exception("*** ERROR (3) invalid OrderInfo")    
    if not oi.advanced or oi.advanced.condition != sob.CONDITION_TRAILING_STOP_ACTIVE:
        raise Exception("*** ERROR (3) invalid OrderInfo.advanced")         
    

def test_TrailingBracket(): 
    def check_aot(aot, cond, trigger, stop_nticks, target_nticks, tag):              
        check_val(aot.condition, cond, "TrailingBracket - " + tag + " - .condition")
        check_val(aot.trigger, trigger, "TrailingBracket - " + tag + " - .trigger")
        check_val(aot.stop_nticks, stop_nticks, 
                  "TrailingBracket - " + tag + " - .stop_nticks")
        check_val(aot.target_nticks, target_nticks, 
                  "TrailingBracket - " + tag + " - .target_nticks")  
    
    aot2 = sob.AdvancedOrderTicketTrailingBracket.build(NTICKS, NTICKS, sob.TRIGGER_FILL_FULL)
    check_aot(aot2, sob.CONDITION_TRAILING_BRACKET, sob.TRIGGER_FILL_FULL, 
              NTICKS, NTICKS, '')

    o1 = 0
    def stop_cb(msg, id1, id2, price, size):
        nonlocal o1   
        if msg == sob.MSG_TRIGGER_BRACKET_OPEN or msg == sob.MSG_TRIGGER_BRACKET_CLOSE:         
            o1 = id2
            
    book = sob.SimpleOrderbook(TICK_TYPE, BOOK_MIN, BOOK_MAX)
    o1 = book.buy_limit(BOOK_MID, SZ, callback=stop_cb, advanced=aot2)       
    oi = book.get_order_info(o1)    
    if not oi.advanced or oi.advanced.condition != sob.CONDITION_TRAILING_BRACKET:
        raise Exception("*** ERROR (1a) invalid OrderInfo.advanced")          
    
    book.sell_limit(BOOK_MID, SZ)
    oi = book.get_order_info(o1)    
    if not oi.advanced or oi.advanced.condition != sob.CONDITION_TRAILING_BRACKET_ACTIVE:
        raise Exception("*** ERROR (1b) invalid OrderInfo.advanced")           
        
    book.sell_limit(BOOK_MID+BOOK_INCR, SZ)
    book.buy_market(SZ)
                    
    oi = book.get_order_info(o1)
    if not oi.advanced or oi.advanced.stop != (BOOK_MID - (NTICKS-1)*BOOK_INCR):
        raise Exception("*** ERROR (1c) invalid OrderInfo.advanced") 
    
    book.buy_market(SZ)    
    if book.market_depth():
        raise Exception("*** ERROR orders still exists in book ***")  
    

def test_all():
    test_OCO()
    print("*** OCO - SUCCESS ***")
    test_OTO()
    print("*** OTO - SUCCESS ***")
    test_FOK()
    print("*** FOK - SUCCESS ***")
    test_BRACKET()
    print("*** BRACKET - SUCCESS ***")
    test_TrailingStop()
    print("*** TrailingStop - SUCCESS ***")
    test_TrailingBracket()
    print("*** TrailingBracket - SUCCESS ***")
    test_AON()
    print("*** AON - SUCCESS ***")
    print("*** SUCCESS ****")


if __name__ == '__main__':
    print("simpleorderbook: ")
    print("  path: ", sob.__file__)
    print("  build datetime: ", sob._BUILD_DATETIME)
    print("  build is debug: ", sob._BUILD_IS_DEBUG)    
    test_all()

    
