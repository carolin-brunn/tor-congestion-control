# -*- coding: utf-8 -*-
"""
Spyder Editor

This is a temporary script file.
"""

import pandas as pd
import numpy as np
import re
import matplotlib.pyplot as plt


numeric_const_pattern = r"[-+]?\d*\.?\d+|[-+]?\d+"
rx = re.compile(numeric_const_pattern, re.VERBOSE)

#r"""[-+]? # optional sign 
#       (?:
#        (?: \d* \. \d+ ) # .1 .12 .123 etc 9.1 etc 98.1 etc |
#        (?: \d+ \.? ) # 1. 12. 123. etc 1 12 123 etc
#        # followed by optional exponent part if desired
#        (?: [Ee] [+-]? \d+ ) ?
#        )"""

#EOQ(D,p,ck,ch):
def process_row(l, idx):
    tmp = float((rx.findall(l[0]))[idx])
    #l_no.append(numbers)
    return tmp
    
    
def main():

    sim_t = "15s"
    congFlavor = "nola" #westwood/nola/vegas
    bdpFlavor = "sendme" #//sendme/cwnd/inflight
    bwRate = "2"
    bwBurst = "2"
    n_circ = 1
    oldCongControl = "0"
    
    
    path = "/home/carolin/Schreibtisch/uni/masterProject/git/my-predictor-congestion/"
    fn = "tor_simu_" + sim_t + "_" + congFlavor + "_" + bdpFlavor + "_" + "rate" + bwRate + "_burst" + bwBurst + "_ncirc" + str(n_circ) + "_oldCong" + oldCongControl + ".txt"
    df = pd.read_csv((path+fn), header = None)
    print(df.head())
    
    df_circ1 = df[df[0].str.contains("Circuit 1")] 
    df_circ1 = df_circ1.reset_index(drop = True)
    if(n_circ > 1):
        df_circ2 = df[df[0].str.contains("Circuit 2")] 
        df_circ2 = df_circ2.reset_index(drop = True)
    if(n_circ > 2):
        df_circ3 = df[df[0].str.contains("Circuit 3")] 
        df_circ3 = df_circ3.reset_index(drop = True)
        
    """
    PLOT
    """    
    fig, [ax1, ax2, ax3] = plt.subplots(nrows=3, ncols=1, sharex=True)
    
    """
    Circuit 1
    """
    ### extract data for exit ###
    df_exit1 = df_circ1[df_circ1[0].str.contains("exit")] 
    df_exit1 = df_exit1.reset_index(drop = True)

    ### extract numerical values ###
    l_exit_time1 = df_exit1.apply(lambda row: process_row(row, 0), axis=1)
    print(l_exit_time1[0:4])
    
    l_exit_cwnd1 = df_exit1.apply(lambda row: process_row(row, 3), axis=1)
    print(l_exit_cwnd1[0:4])
    
    l_exit_inflight1 = df_exit1.apply(lambda row: process_row(row, 4), axis=1)
    print(l_exit_inflight1[0:4])
    
    l_exit_pckcnt1 = df_exit1.apply(lambda row: process_row(row, 8), axis=1)
    print(l_exit_pckcnt1[0:4])
    final_cnt = l_exit_pckcnt1[len(l_exit_pckcnt1)-1]
    
    # plot
    ax1.plot(l_exit_time1, l_exit_cwnd1, color = "black", label = "Circuit 1")
    ax2.plot(l_exit_time1, l_exit_inflight1, color = "black" )
    ax3.plot(l_exit_time1, l_exit_pckcnt1, color = "black")
    
    ax1.set_title("Congestion Window")
    ax1.set_ylabel("Window size")
    ax2.set_title("Inflight")
    ax2.set_ylabel("Inflight")
    ax3.set_title("Packet Count")
    ax3.set_xlabel("Time in ns")
    ax3.set_ylabel("Packet Count")
    
    
    """
    Circuit 2
    """
    if(n_circ > 1):
        ### extract data for exit ###
        df_exit2 = df_circ2[df_circ2[0].str.contains("exit")] 
        df_exit2 = df_exit2.reset_index(drop = True)
    
        ### extract numerical values ###
        l_exit_time2 = df_exit2.apply(lambda row: process_row(row, 0), axis=1)
        print(l_exit_time2[0:4])
        
        l_exit_cwnd2 = df_exit2.apply(lambda row: process_row(row, 3), axis=1)
        print(l_exit_cwnd2[0:4])
        
        l_exit_inflight2 = df_exit2.apply(lambda row: process_row(row, 4), axis=1)
        print(l_exit_inflight2[0:4])
        
        l_exit_pckcnt2 = df_exit2.apply(lambda row: process_row(row, 8), axis=1)
        print(l_exit_pckcnt2[0:4])
        final_cnt2 = l_exit_pckcnt2[len(l_exit_pckcnt2)-1]
        
        # plot
        ax1.plot(l_exit_time2, l_exit_cwnd2, color = "blue", label = "Circuit 2")
        ax2.plot(l_exit_time2, l_exit_inflight2, color = "blue")
        ax3.plot(l_exit_time2, l_exit_pckcnt2, color = "blue")
    
    """
    Circuit 3
    """
    if(n_circ > 2):
        ### extract data for exit ###
        df_exit3 = df_circ3[df_circ3[0].str.contains("exit")] 
        df_exit3 = df_exit3.reset_index(drop = True)
    
        ### extract numerical values ###
        l_exit_time3 = df_exit3.apply(lambda row: process_row(row, 0), axis=1)
        print(l_exit_time3[0:4])
        
        l_exit_cwnd3 = df_exit3.apply(lambda row: process_row(row, 3), axis=1)
        print(l_exit_cwnd3[0:4])
        
        l_exit_inflight3 = df_exit3.apply(lambda row: process_row(row, 4), axis=1)
        print(l_exit_inflight3[0:4])
        
        l_exit_pckcnt3 = df_exit3.apply(lambda row: process_row(row, 8), axis=1)
        print(l_exit_pckcnt3[0:4])
        final_cnt = l_exit_pckcnt3[len(l_exit_pckcnt3)-1]
        
        ax1.plot(l_exit_time3, l_exit_cwnd3, color = "orange", label = "Circuit 3")
        ax2.plot(l_exit_time3, l_exit_inflight3, color = "orange")
        ax3.plot(l_exit_time3, l_exit_pckcnt3, color = "orange")
    
    
    fig.legend(loc = "right")


    
if __name__ == "__main__":
    main()