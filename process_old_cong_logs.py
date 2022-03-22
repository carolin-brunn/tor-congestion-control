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
    congFlavor = "westwood" #westwood/nola/vegas
    bdpFlavor = "piecewise" #//sendme/cwnd/inflight
    bwRate = "2"
    bwBurst = "2"
    n_circ = "3"
    oldCongControl = "1"
    
    
    path = "/home/carolin/Schreibtisch/uni/masterProject/git/my-predictor-congestion/"
    fn = "tor_simu_" + sim_t + "_" + congFlavor + "_" + bdpFlavor + "_" + "rate" + bwRate + "_burst" + bwBurst + "_ncirc" + n_circ + "_oldCong" + oldCongControl + ".txt"
    df = pd.read_csv((path+fn), header = None)
    print(df.head())
    
    df_circ1 = df[df[0].str.contains("Circuit 1")] 
    df_circ1 = df_circ1.reset_index(drop = True)
    if(n_circ == "2"):
        df_circ2 = df[df[0].str.contains("Circuit 2")] 
        df_circ2 = df_circ2.reset_index(drop = True)
    if(n_circ == "3"):
        df_circ2 = df[df[0].str.contains("Circuit 2")] 
        df_circ2 = df_circ2.reset_index(drop = True)
        
        df_circ3 = df[df[0].str.contains("Circuit 3")] 
        df_circ3 = df_circ3.reset_index(drop = True)
        
    
    """
    Circuit 1
    """
    
    ### extract data for exit ###
    df_exit1 = df_circ1[df_circ1[0].str.contains("exit")] 
    #df_exit = df_exit.reset_index(drop = True)

    ### extract numerical values ###
    l_exit_time1 = df_exit1.apply(lambda row: process_row(row, 0), axis=1)
    print(l_exit_time1[0:4])
    
    l_exit_pckwdw1 = df_exit1.apply(lambda row: process_row(row, 3), axis=1)
    print(l_exit_pckwdw1[0:4])
    
    l_exit_pckcnt1 = df_exit1.apply(lambda row: process_row(row, 4), axis=1)
    print(l_exit_pckcnt1[0:4])
    
    #plt.scatter(l_exit_time1, l_exit_pckcnt1, color = "black", label = "Circ1: Package Cnt")
    #plt.plot(l_exit_time1, l_exit_pckwdw1, color = "black", label = "Circ1: Package Wdw")
    
    """
    Circuit 2
    """
    ### extract data for exit ###
    df_exit2 = df_circ2[df_circ2[0].str.contains("exit")] 
    #df_exit = df_exit.reset_index(drop = True)
    
    ### extract numerical values ###
    l_exit_time2 = df_exit2.apply(lambda row: process_row(row, 0), axis=1)
    print(l_exit_time2[0:4])
    
    l_exit_pckwdw2 = df_exit2.apply(lambda row: process_row(row, 3), axis=1)
    print(l_exit_pckwdw2[0:4])
    
    l_exit_pckcnt2 = df_exit2.apply(lambda row: process_row(row, 4), axis=1)
    print(l_exit_pckcnt2[0:4])
    
    #plt.scatter(l_exit_time2, l_exit_pckcnt2, color = "blue", label = "Circ2: Package Wdw")
    #plt.plot(l_exit_time2, l_exit_pckwdw2, color = "blue", label = "Circ2: Package Wdw")
    
    """
    Circuit 3
    """
    
    ### extract data for exit ###
    df_exit3 = df_circ3[df_circ3[0].str.contains("exit")] 
    #df_exit = df_exit.reset_index(drop = True)
    
    ### extract numerical values ###
    l_exit_time3 = df_exit3.apply(lambda row: process_row(row, 0), axis=1)
    print(l_exit_time3[0:4])
    
    l_exit_pckwdw3 = df_exit3.apply(lambda row: process_row(row, 3), axis=1)
    print(l_exit_pckwdw3[0:4])
    
    l_exit_pckcnt3 = df_exit3.apply(lambda row: process_row(row, 4), axis=1)
    print(l_exit_pckcnt3[0:4])
    
    #plt.scatter(l_exit_time3, l_exit_pckcnt3, color = "orange", label = "Circ3: Package Wdw")
    #plt.plot(l_exit_time3, l_exit_pckwdw3, color = "orange", label = "Circ3: Package Wdw")
    
    
    fig, [ax1, ax2] = plt.subplots(nrows=2, ncols=1, sharex=True)
    
    ax1.plot(l_exit_time1, l_exit_pckwdw1, color = "black", label = "Circ1: Package Wdw")
    ax1.plot(l_exit_time2, l_exit_pckwdw2, color = "blue", label = "Circ2: Package Wdw")
    ax1.plot(l_exit_time3, l_exit_pckwdw3, color = "orange", label = "Circ3: Package Wdw")
    ax1.set_title("Package Window")
    #ax1.set_xlabel("Time in ns")
    ax1.set_ylabel("Window size")
    
    ax2.scatter(l_exit_time1, l_exit_pckcnt1, color = "black", label = "Circ1: Package Wdw")
    ax2.scatter(l_exit_time2, l_exit_pckcnt2, color = "blue", label = "Circ2: Package Wdw")
    ax2.scatter(l_exit_time3, l_exit_pckcnt3, color = "orange", label = "Circ3: Package Wdw")
    ax2.set_title("Packet Count", pad = 0)
    ax2.set_xlabel("Time in ns")
    ax2.set_ylabel("Packet count")
    
    
    
    fig.legend(loc = "right")

    
    #plt.savefig("normal_cong_exitpck_proxydel_"+ sim_time + ".pdf")
    
    

    
if __name__ == "__main__":
    main()