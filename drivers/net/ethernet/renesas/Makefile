# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Renesas network device drivers
#

obj-$(CONFIG_SH_ETH) += sh_eth.o

ravb-objs := ravb_main.o ravb_ptp.o
obj-$(CONFIG_RAVB) += ravb.o

obj-$(CONFIG_RENESAS_ETHER_SWITCH) += rswitch.o

obj-$(CONFIG_RENESAS_GEN4_PTP) += rcar_gen4_ptp.o

obj-$(CONFIG_RTSN) += rtsn.o

obj-$(CONFIG_RZT2_ETHSS) += rzt2_ethss.o rzt2_timer_hwtstamp.o

obj-$(CONFIG_RENESAS_ETHER_ESWM_T2N) += rzt2n_eswm_ptp.o rzt2n_eswm.o rzt2n_eswm_xdp.o

obj-$(CONFIG_RENESAS_ETHER_HPSW_RZT2N) += rzt2n_hpsw.o
