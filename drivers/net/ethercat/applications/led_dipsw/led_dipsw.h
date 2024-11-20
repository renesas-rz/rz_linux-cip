#include "../../soes/esc_coe.h"
#include "utypes.h"
#include "ecat_options.h"

#ifndef HW_REV
#define HW_REV "1.0"
#endif

#ifndef SW_REV
#define SW_REV "1.0"
#endif

/* Application variables */
struct _Objects Obj;

struct led_dipsw_device {
	struct gpio_desc *led0, *led1, *led2, *led3;
	struct gpio_desc *dipsw0, *dipsw1, *dipsw2, *dipsw3;
} led_dipsw;

static const char acName1000[] = "Device Type";
static const char acName1008[] = "Device Name";
static const char acName1009[] = "Hardware Version";
static const char acName100A[] = "Software Version";
static const char acName1018[] = "Identity Object";
static const char acName1018_00[] = "Max SubIndex";
static const char acName1018_01[] = "Vendor ID";
static const char acName1018_02[] = "Product Code";
static const char acName1018_03[] = "Revision Number";
static const char acName1018_04[] = "Serial Number";
static const char acName1601[] = "RxPDO-Map";
static const char acName1601_00[] = "Max SubIndex";
static const char acName1601_01[] = "LED";
static const char acName1A00[] = "TxPDO-Map";
static const char acName1A00_00[] = "Max SubIndex";
static const char acName1A00_01[] = "BUTTON";
static const char acName1C00[] = "Sync Manager Communication Type";
static const char acName1C00_00[] = "Max SubIndex";
static const char acName1C00_01[] = "Communications Type SM0";
static const char acName1C00_02[] = "Communications Type SM1";
static const char acName1C00_03[] = "Communications Type SM2";
static const char acName1C00_04[] = "Communications Type SM3";
static const char acName1C12[] = "RxPDO assign";
static const char acName1C12_00[] = "Max SubIndex";
static const char acName1C12_01[] = "PDO Mapping";
static const char acName1C13[] = "TxPDO assign";
static const char acName1C13_00[] = "Max SubIndex";
static const char acName1C13_01[] = "PDO Mapping";
static const char acName6000[] = "BUTTON";
static const char acName7010[] = "LED";

const struct _objd SDO1000[] = {
	{0x0, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1000, 0x00005001, NULL},
};

const struct _objd SDO1008[] = {
	{0x0, DTYPE_VISIBLE_STRING, 72, ATYPE_RO, acName1008, 0, "RZ/T2 EtherCAT"},
};

const struct _objd SDO1009[] = {
	{0x0, DTYPE_VISIBLE_STRING, 24, ATYPE_RO, acName1009, 0, HW_REV},
};

const struct _objd SDO100A[] = {
	{0x0, DTYPE_VISIBLE_STRING, 24, ATYPE_RO, acName100A, 0, SW_REV},
};

const struct _objd SDO1018[] = {
	{0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1018_00, 4, NULL},
	{0x01, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1018_01, 0x00000766, NULL},
	{0x02, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1018_02, 0x00000800, NULL},
	{0x03, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1018_03, 0x00000200, NULL},
	{0x04, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1018_04, 0x00000000, NULL},
};

const struct _objd SDO1601[] = {
	{0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1601_00, 1, NULL},
	{0x01, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1601_01, 0x70100020, NULL},
};

const struct _objd SDO1A00[] = {
	{0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1A00_00, 1, NULL},
	{0x01, DTYPE_UNSIGNED32, 32, ATYPE_RO, acName1A00_01, 0x60000020, NULL},
};

const struct _objd SDO1C00[] = {
	{0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1C00_00, 4, NULL},
	{0x01, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1C00_01, 1, NULL},
	{0x02, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1C00_02, 2, NULL},
	{0x03, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1C00_03, 3, NULL},
	{0x04, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1C00_04, 4, NULL},
};

const struct _objd SDO1C12[] = {
	{0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1C12_00, 1, NULL},
	{0x01, DTYPE_UNSIGNED16, 16, ATYPE_RO, acName1C12_01, 0x1601, NULL},
};

const struct _objd SDO1C13[] = {
	{0x00, DTYPE_UNSIGNED8, 8, ATYPE_RO, acName1C13_00, 1, NULL},
	{0x01, DTYPE_UNSIGNED16, 16, ATYPE_RO, acName1C13_01, 0x1A00, NULL},
};

const struct _objd SDO6000[] = {
	{0x0, DTYPE_UNSIGNED32, 32, ATYPE_RO | ATYPE_TXPDO, acName6000, 0, &Obj.BUTTON},
};

const struct _objd SDO7010[] = {
	{0x0, DTYPE_UNSIGNED32, 32, ATYPE_RO | ATYPE_RXPDO, acName7010, 0, &Obj.LED},
};

const struct _objectlist SDOobjects[] = {
	{0x1000, OTYPE_VAR, 0, 0, acName1000, SDO1000},
	{0x1008, OTYPE_VAR, 0, 0, acName1008, SDO1008},
	{0x1009, OTYPE_VAR, 0, 0, acName1009, SDO1009},
	{0x100A, OTYPE_VAR, 0, 0, acName100A, SDO100A},
	{0x1018, OTYPE_RECORD, 4, 0, acName1018, SDO1018},
	{0x1601, OTYPE_RECORD, 1, 0, acName1601, SDO1601},
	{0x1A00, OTYPE_RECORD, 1, 0, acName1A00, SDO1A00},
	{0x1C00, OTYPE_ARRAY, 4, 0, acName1C00, SDO1C00},
	{0x1C12, OTYPE_ARRAY, 1, 0, acName1C12, SDO1C12},
	{0x1C13, OTYPE_ARRAY, 1, 0, acName1C13, SDO1C13},
	{0x6000, OTYPE_VAR, 0, 0, acName6000, SDO6000},
	{0x7010, OTYPE_VAR, 0, 0, acName7010, SDO7010},
	{0xffff, 0xff, 0xff, 0xff, NULL, NULL}
};
