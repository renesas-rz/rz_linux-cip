/*
 * Renesas RZ/V2M GICD hedaer file
 *
 * Copyright (C) 2020 Renesas Electronics Corporation
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __IRQ_GIC_RENESAS_RZV2M_H__
#define __IRQ_GIC_RENESAS_RZV2M_H__
const u32 gic_init_target[] =
{
    0x3030303,    //offset address : 0x820
    0x3030303,    //offset address : 0x824
    0x3030303,    //offset address : 0x828
    0x3030303,    //offset address : 0x82C
    0x3030303,    //offset address : 0x830
    0x3030303,    //offset address : 0x834
    0x3030303,    //offset address : 0x838
    0x3030303,    //offset address : 0x83C
    0x3030303,    //offset address : 0x840
    0x3030303,    //offset address : 0x844
    0x1010101,    //offset address : 0x848
    0x2010002,    //offset address : 0x84C
    0x1010201,    //offset address : 0x850
    0x1010101,    //offset address : 0x854
    0x2020101,    //offset address : 0x858
    0x2020202,    //offset address : 0x85C
    0x2020202,    //offset address : 0x860
    0x2020202,    //offset address : 0x864
    0x1010101,    //offset address : 0x868
    0x2020202,    //offset address : 0x86C
    0x2020202,    //offset address : 0x870
    0x1010101,    //offset address : 0x874
    0x2010101,    //offset address : 0x878
    0x1010101,    //offset address : 0x87C
    0x1010101,    //offset address : 0x880
    0x1010101,    //offset address : 0x884
    0x2010101,    //offset address : 0x888
    0x2020202,    //offset address : 0x88C
    0x1020202,    //offset address : 0x890
    0x1010101,    //offset address : 0x894
    0x1010101,    //offset address : 0x898
    0x1010101,    //offset address : 0x89C
    0x2010101,    //offset address : 0x8A0
    0x2020202,    //offset address : 0x8A4
    0x2020202,    //offset address : 0x8A8
    0x2020202,    //offset address : 0x8AC
    0x2020202,    //offset address : 0x8B0
    0x2020202,    //offset address : 0x8B4
    0x2020202,    //offset address : 0x8B8
    0x2020202,    //offset address : 0x8BC
    0x2020202,    //offset address : 0x8C0
    0x2020000,    //offset address : 0x8C4
    0x2020202,    //offset address : 0x8C8
    0x2020202,    //offset address : 0x8CC
    0x2020202,    //offset address : 0x8D0
    0x2020202,    //offset address : 0x8D4
    0x2020202,    //offset address : 0x8D8
    0x2020202,    //offset address : 0x8DC
    0x2020202,    //offset address : 0x8E0
    0x2020202,    //offset address : 0x8E4
    0x2020202,    //offset address : 0x8E8
    0x2020202,    //offset address : 0x8EC
    0x2020202,    //offset address : 0x8F0
    0x2020202,    //offset address : 0x8F4
    0x2020202,    //offset address : 0x8F8
    0x2020202,    //offset address : 0x8FC
    0x1010202,    //offset address : 0x900
    0x2010201,    //offset address : 0x904
    0x2010201,    //offset address : 0x908
    0x2010201,    //offset address : 0x90C
    0x1010201,    //offset address : 0x910
    0x10101,    //offset address : 0x914
    0x1000000,    //offset address : 0x918
    0x1010101,    //offset address : 0x91C
    0x1010101,    //offset address : 0x920
    0x1010101,    //offset address : 0x924
    0x1010101,    //offset address : 0x928
    0x1010101,    //offset address : 0x92C
    0x1010101,    //offset address : 0x930
    0x1010101,    //offset address : 0x934
    0x0,    //offset address : 0x938
    0x0,    //offset address : 0x93C
    0x0,    //offset address : 0x940
    0x0,    //offset address : 0x944
    0x0,    //offset address : 0x948
    0x0,    //offset address : 0x94C
    0x0,    //offset address : 0x950
    0x1010100,    //offset address : 0x954
    0x1010101,    //offset address : 0x958
    0x1010101,    //offset address : 0x95C
    0x1010101,    //offset address : 0x960
    0x1010101,    //offset address : 0x964
    0x2020101,    //offset address : 0x968
    0x2020002,    //offset address : 0x96C
    0x2020202,    //offset address : 0x970
    0x2020202,    //offset address : 0x974
    0x3030302,    //offset address : 0x978
    0x2020203,    //offset address : 0x97C
    0x1010102,    //offset address : 0x980
    0x1010101,    //offset address : 0x984
    0x1010101,    //offset address : 0x988
    0x1010101,    //offset address : 0x98C
    0x2020202,    //offset address : 0x990
    0x2020202,    //offset address : 0x994
    0x1010101,    //offset address : 0x998
    0x1010101,    //offset address : 0x99C
    0x1010202,    //offset address : 0x9A0
    0x2020203,    //offset address : 0x9A4
};
#endif /* __IRQ_GIC_RENESAS_RZV2M_H__ */

