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
    bdpFlavor = "piecewise" #//sendme/cwnd/inflight
    bwRate = "2"
    bwBurst = "6"
    #filename = "my_cong_control_impl/tor_simu_" + sim_time + ".txt"
    path = "/home/carolin/Schreibtisch/uni/masterProject/git/my-predictor-congestion/"
    fn = "tor_simu_" + sim_t + "_" + congFlavor + "_" + bdpFlavor + "_" + "rate" + bwRate + "_burst" + bwBurst + "_rtt" + "100" + ".txt"
    
    df = pd.read_csv((path+fn), header = None)
    print(df.head())
    
    ### extract data for exit ###
    df_exit = df[df[0].str.contains("exit1")] 
    df_exit = df_exit.reset_index(drop = True)
    
    # package window
    df_exit_pck = df_exit[df_exit[0].str.contains("Package")] 
    df_exit_pck = df_exit_pck.reset_index(drop = True)
    
    # deliver window
    df_exit_del = df_exit[df_exit[0].str.contains("Deliver")] 
    df_exit_del = df_exit_del.reset_index(drop = True)
    
    ### extract numerical values ###
    l_exit_time = df_exit.apply(lambda row: process_row(row, 0), axis=1)
    print(l_exit_time[0:4])
    
    l_exit_curr_cwnd = df_exit.apply(lambda row: process_row(row, 3), axis=1)
    print(l_exit_curr_cwnd[0:4])
    
    l_exit_update_cwnd = df_exit.apply(lambda row: process_row(row, 5), axis=1)
    print(l_exit_update_cwnd[0:4])
    
    l_pck_counter = df_exit.apply(lambda row: process_row(row, 6), axis=1)
    print(l_pck_counter[0:4])
    final_cnt = l_pck_counter[len(l_pck_counter)-1]
    
    plt.plot(l_exit_time, l_exit_curr_cwnd, color = "blue", label = "Exit: Cwnd")
    
    plt.text(10, -10.5, ("Cnt: "+str(final_cnt)))
    plt.legend()
    plt.xlabel("Time in ns")
    plt.ylabel("Congestion window size")
    plt.show()
    
    """
    
    l_exit_pck_time = df_exit_pck.apply(lambda row: process_row(row, 0), axis=1)
    l_exit_pck = df_exit_pck.apply(lambda row: process_row(row, 3), axis=1)
    print(l_exit_pck[0:4])
    
    l_exit_del_time = df_exit_del.apply(lambda row: process_row(row, 0), axis=1)
    l_exit_del = df_exit_del.apply(lambda row: process_row(row, 3), axis=1)
    print(l_exit_del[0:4])
    
    plt.plot(l_exit_pck_time, l_exit_pck, color = "black", label = "Exit: Package Wdw")
    #plt.plot(l_exit_del_time, l_exit_del, color = "blue")
    #plt.show()
    #plt.close()


    ### extract data for proxy ###
    df_proxy = df[df[0].str.contains("proxy1")] 
    df_proxy = df_proxy.reset_index(drop = True)
    
    # package window
    df_proxy_pck = df_proxy[df_proxy[0].str.contains("Package")] 
    df_proxy_pck = df_proxy_pck.reset_index(drop = True)
    
    # deliver window
    df_proxy_del = df_proxy[df_proxy[0].str.contains("Deliver")] 
    df_proxy_del = df_proxy_del.reset_index(drop = True)
    
    ### extract numerical values ###
    l_proxy_time = df_proxy.apply(lambda row: process_row(row, 0), axis=1)
    print(l_proxy_time[0:4])
    
    l_proxy_pck_time = df_proxy_pck.apply(lambda row: process_row(row, 0), axis=1)
    l_proxy_pck = df_proxy_pck.apply(lambda row: process_row(row, 3), axis=1)
    print(l_proxy_pck[0:4])
    
    l_proxy_del_time = df_proxy_del.apply(lambda row: process_row(row, 0), axis=1)
    l_proxy_del = df_proxy_del.apply(lambda row: process_row(row, 3), axis=1)
    print(l_proxy_del[0:4])
    
    #plt.plot(l_proxy_pck_time, l_proxy_pck, color = "black")
    plt.plot(l_proxy_del_time, l_proxy_del, color = "blue", label = "Proxy: Deliver Wdw")
    plt.legend()
    plt.xlabel("Time in ns")
    plt.ylabel("Window size")
    
    plt.savefig("normal_cong_exitpck_proxydel_"+ sim_time + ".pdf")
    
    """

    
if __name__ == "__main__":
    main()