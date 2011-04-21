/*
 * SIS-IS Test program.
 * Stephen Sigwart
 * University of Delaware
 */

#include "sisis_api.h"

#ifndef _SISIS_ADDR_FORMAT_H
#define _SISIS_ADDR_FORMAT_H

// SIS-IS address component info
static sisis_component_t components[] = {
	{
		"prefix",
		16,
		SISIS_COMPONENT_FIXED,
		0xfcff
	},
	{
		"sisis_version",
		5,
		SISIS_COMPONENT_FIXED,
		2
	},
	{
		"process_type",
		16,
		0,
		0
	},
	{
		"process_version",
		5,
		0,
		0
	},
	{
		"sys_id",
		32,
		0,
		0
	},
	{
		"pid",
		22,
		0,
		0
	},
	{
		"timestamp",
		32,
		0,
		0
	}
}
static int num_components = sizeof(components);

#endif
