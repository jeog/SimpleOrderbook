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
    
    def check_aot(aot, cond, trigger, is_buy, sz, loss_stop, loss_limit, 
                  target_limit, tag):
        check_val(aot.condition, cond, "BRACKET - " + tag + " - .condition")
        check_val(aot.trigger, trigger, "BRACKET - " + tag + " - .trigger") 
        check_val(aot.is_buy, is_buy, "BRACKET - " + tag + " - .is_buy")
        check_val(aot.size, sz, "BRACKET - " + tag + " - .size")
        check_val(aot.loss_stop, loss_stop, "BRACKET - " + tag + " - .loss_stop")  
        check_val(aot.loss_limit, loss_limit, "BRACKET - " + tag + " - .loss_limit")                     
        check_val(aot.target_limit, target_limit, 
                  "BRACKET - " + tag + " - .target_limit")
            
    aot_ssl = AOT.build_sell_stop_limit(BOOK_MID, BOOK_MIN, BOOK_MAX, SZ)        
    check_aot(aot_ssl, sob.CONDITION_BRACKET, DEF_TRIGGER, False, SZ, BOOK_MID, 
              BOOK_MIN, BOOK_MAX, 'sell_stop_limit fill-partial AOT')

    aot_bsl = AOT.build_buy_stop_limit(BOOK_MID, BOOK_MAX, BOOK_MIN, SZ*2,
                                       sob.TRIGGER_FILL_FULL)        
    check_aot(aot_bsl, sob.CONDITION_BRACKET, sob.TRIGGER_FILL_FULL, True, SZ*2, 
              BOOK_MID, BOOK_MAX, BOOK_MIN, 'buy_stop_limit fill-full AOT')    

    aot_ss = AOT.build_sell_stop(BOOK_MID, BOOK_MAX, 1, sob.TRIGGER_FILL_FULL)        
    check_aot(aot_ss, sob.CONDITION_BRACKET, sob.TRIGGER_FILL_FULL, False, 1, 
              BOOK_MID, 0, BOOK_MAX, 'sell_stop fill-full AOT') 

    aot_bs = AOT.build_buy_stop(BOOK_MID, BOOK_MIN, 100*SZ)        
    check_aot(aot_bs, sob.CONDITION_BRACKET, DEF_TRIGGER, True, 100*SZ, 
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
    if md[BOOK_MIN][0] != 100*SZ:
        raise Exception("*** ERROR reaming size @ %f (%i) != %i ***" % 
                        (BOOK_MIN, md[BOOK_MIN][0],100*SZ))    
    if md[BOOK_MIN][1] != sob.SIDE_BID:
        raise Exception("*** ERROR bracket target not on correct side of market")
    
    oi = book.get_order_info(o1)
    if not oi.advanced or oi.advanced.condition != sob.CONDITION_BRACKET_ACTIVE:
        raise Exception("*** ERROR (2b) invalid OrderInfo.advanced") 
    
    book.sell_limit(BOOK_MIN, SZ)
    md = book.market_depth()
    if md[BOOK_MIN][0] != 99*SZ:
        raise Exception("*** ERROR orders still exists in book ***")  
               
    try:
        book.buy_market(1) # check nothing above
    except:
        return
    
    raise Exception("*** ERROR sell orders were still active ***")
    
    
def test_TrailingStop():  
    def check_aot(aot, cond, trigger, nticks, tag):              
        check_val(aot.condition, cond, "TrailingStop - " + tag + " - .condition")
        check_val(aot.trigger, trigger, "TrailingStop - " + tag + " - .trigger")
        check_val(aot.nticks, nticks, "TrailingStop - " + tag + " - .nticks")  
            
    aot1 = sob.AdvancedOrderTicketTrailingStop.build(NTICKS)
    check_aot(aot1, sob.CONDITION_TRAILING_STOP, sob.TRIGGER_FILL_FULL, NTICKS, '')

    o1 = 0
    def stop_cb(msg, id1, id2, price, size):
        nonlocal o1   
        if msg == sob.MSG_TRIGGER_TRAILING_STOP:         
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
    
    aot2 = sob.AdvancedOrderTicketTrailingBracket.build(NTICKS, NTICKS)
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
    print("*** SUCCESS ****")


if __name__ == '__main__':
    test_all()

    