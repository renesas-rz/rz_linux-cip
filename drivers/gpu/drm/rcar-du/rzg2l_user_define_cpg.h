// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/G2L User Define CPG Parameter
 *
 * Copyright (C) 2021 Renesas Electronics Corporation
 */

#define RZG2L_USER_DEFINE_CPG_ENABLE	0

#if RZG2L_USER_DEFINE_CPG_ENABLE
struct cpg_param mipi_dsi_output_param =
{
		/* VGA 25.175MHz	*/
		/* frequency		*/	25175,
		/* pl5_refdiv		*/	2,
		/* pl5_intin		*/	125,
		/* pl5_fracin		*/	14680064,
		/* pl5_postdiv1		*/	5,
		/* pl5_postdiv2		*/	1,
		/* pl5_divval		*/	0,
		/* pl5_spread		*/	0x16,
		/* dsi_div_a		*/	1,	// 1/2
		/* dsi_div_b		*/	2	// 1/3
};

struct cpg_param parallel_output_param =
{
		/* VGA 25.175MHz	*/
		/* frequency		*/	25175,
		/* pl5_refdiv		*/	1,
		/* pl5_intin		*/	102,
		/* pl5_fracin		*/	13386820,
		/* pl5_postdiv1		*/	7,
		/* pl5_postdiv2		*/	7,
		/* pl5_divval		*/	0,
		/* pl5_spread		*/	0x16,
		/* dsi_div_a		*/	1,	// 1/2
		/* dsi_div_b		*/	0	// 1/1
};
#endif /* RZG2L_USER_DEFINE_CPG_ENABLE */
