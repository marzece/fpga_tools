from lmk import LMK
from copy import deepcopy

def VCO_Output(lmk):
    selected = lmk.fields["VCO_MUX"].value
    if(selected == 0):
        return list(filter(lambda x: x.name=="VCO0", lmk.clocks))[0]
    elif(selected == 1):
        return list(filter(lambda x: x.name=="VCO1", lmk.clocks))[0]
    elif(selected == 2):
        return list(filter(lambda x: x.name=="OSCin", lmk.clocks))[0]
    else:
        print("VCO MUX has a weird value")
        raise Exception

def SYSRef_Output(lmk):
    lmk.fields["SYNC_EN"]
    # First check if the block is enable
    if(lmk.fields["SYNC_EN"].value != 0x1):
        print("SYSREF not enabled in SYNC_EN")
        raise Exception

    clock = deepcopy(VCO_Output(lmk))
    

    if(lmk.fields["SYSREF_CLKin0_MUX"].value != 0):
        raise Exception("CLKIn0 selected as SYSREF source! Thats probably a mistake")

    sysref_mux = lmk.fields["SYSREF_MUX"].value
    if(sysref_mux == 0x0):
        return NotImplemented
        # Normal SYNC
    elif(sysref_mux == 0x1):
        return NotImplemented
        # Re-Clocked
    elif(sysref_mux == 0x2):
        return NotImplemented
        # SYSREF Pulser
        
    elif(sysref_mux == 0x3):
        #SYSREF_CONTINUOUS
        sysref_divider = lmk.fields["SYSREF_DIV"].value
        if(sysref_divider < 8):
            raise Exception("Divider value less than 8 not allowed")
        clock.freq = clock.freq / sysref_divider
        clock.name = clock.name + "-SYSREF"
        return clock

    #lmk.fields["SYSREF_DDLY_PD"]
    #lmk.fields["SYSREF_DDLY"]
    #lmk.fields["SYSREF_CLKin0_MUX"]

def CLKOut(lmk, clk_num):
    # Even Number clk outptus are DCLKOuts
    # Odd number are SDClkOuts (sysref)
    pll2_clock = VCO_Output(lmk)

    dclock_mux = lmk.fields["DCLKout{}_MUX".format(clk_num)]
    divider = lmk.fields["DCLKout{}_DIV".format(clk_num)].value
    if(divider == 0x1 and dclock_mux.value == 0):
        print("Can't have a divider value of 1 on clock %i if DCLKout_MUX is 0" % clk_num)
        raise Exception
    divider = 32 if divider == 0 else divider
    new_clock = deepcopy(pll2_clock)
    new_clock.freq = new_clock.freq / divider
    new_clock.name = new_clock.name + " - " + ("DCLKOut%i" % clk_num)
    return new_clock

def SDClockOut(lmk, clk_num):
    # Even Number clk outptus are DCLKOuts
    # Odd number are SDClkOuts (sysref)
    if(clk_num % 2 !=1 or clk_num >13 or clk_num < 0):
        print("Clock num {} is not valid".format(clk_num))
        return

    if(lmk.fields["SDCLKout{}_FMT".format(clk_num)].value == 0):
        print("SDCLKout{} driver is off!".format(clk_num))
        return


    if(lmk.fields["SDCLKout{}_MUX".format(clk_num)].value == 0x0):
        return CLKOut(lmk, clk_num-1)

    if(lmk.fields["SDCLKout{}_PD".format(clk_num)].value == 1):
        print("SDCLKOut{} powered down".format(clk_num))
        return
    return deepcopy(SYSRef_Output(lmk))

if __name__ == "__main__":
    lmk = LMK("HERMES_Config.cfg")
