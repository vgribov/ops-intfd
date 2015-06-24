/*
 Copyright (C) 2015 Hewlett Packard Enterprise Development LP
 All Rights Reserved.

 Licensed under the Apache License, Version 2.0 (the "License"); you may
 not use this file except in compliance with the License. You may obtain
 a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 License for the specific language governing permissions and limitations
 under the License.
*/
/************************************************************************//**
 * @ingroup intfd
 *
 * @file
 * Source for intfd OVSDB access interface.
 *
 ***************************************************************************/

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <config.h>
#include <command-line.h>
#include <compiler.h>
#include <daemon.h>
#include <dirs.h>
#include <dynamic-string.h>
#include <fatal-signal.h>
#include <ovsdb-idl.h>
#include <poll-loop.h>
#include <unixctl.h>
#include <util.h>
#include <openvswitch/vconn.h>
#include <openvswitch/vlog.h>
#include <vswitch-idl.h>
#include <openhalon-idl.h>
#include <hash.h>
#include <shash.h>

#include "intfd.h"
#include "intfd_utils.h"

VLOG_DEFINE_THIS_MODULE(intfd_ovsdb_if);

/** @ingroup intfd
 * @{ */

static struct ovsdb_idl *idl;

static unsigned int idl_seqno;

static bool system_configured = false;

/* Mapping of all the interfaces. */
static struct shash all_interfaces = SHASH_INITIALIZER(&all_interfaces);

struct intf_user_cfg {
    enum ovsrec_interface_user_config_admin_e   admin_state;
    enum ovsrec_interface_user_config_autoneg_e autoneg;
    enum ovsrec_interface_user_config_pause_e   pause;
    enum ovsrec_interface_user_config_duplex_e  duplex;

    uint32_t    speeds;
    uint32_t    mtu;
};

struct intf_oper_state {
    bool        enabled;
    enum ovsrec_interface_error_e reason;

    enum ovsrec_interface_hw_intf_config_duplex_e   duplex;
    enum ovsrec_interface_hw_intf_config_pause_e    pause;
    bool        autoneg;
    uint32_t    mtu;
    uint32_t    speeds;
    uint32_t    fixed_speed;

};

struct intf_pm_info {
    uint64_t    op_connector_flags;
    enum ovsrec_interface_pm_info_connector_e           connector;
    enum ovsrec_interface_pm_info_connector_status_e    connector_status;

    enum ovsrec_interface_hw_intf_config_interface_type_e   intf_type;
};

struct iface {
    char                    *name;
    struct intf_user_cfg    user_cfg;
    struct intf_oper_state  op_state;
    struct intf_pm_info     pm_info;
};


char *interface_pm_info_connector_strings[] = {
    OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP_CR4,
    OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP_LR4,
    OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP_SR4,
    OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_CX,
    OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_DAC,
    OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_FC,
    OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_LR,
    OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_LRM,
    OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_LX,
    OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_RJ45,
    OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_SR,
    OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_SX,
    OVSREC_INTERFACE_PM_INFO_CONNECTOR_ABSENT,
    OVSREC_INTERFACE_PM_INFO_CONNECTOR_UNKNOWN
};

/* The strings that indicate the autoneg configuration on an interface. */
char *iface_config_autoneg_strings[] = {
    INTERFACE_USER_CONFIG_MAP_AUTONEG_OFF,
    INTERFACE_USER_CONFIG_MAP_AUTONEG_ON,
    INTERFACE_USER_CONFIG_MAP_AUTONEG_DEFAULT
};

static void del_old_interface(struct shash_node *sh_node);

void
intfd_debug_dump(struct ds *ds, int argc, const char *argv[])
{
    struct shash_node *sh_node;
    bool list_all_intf = true;
    const char *interface_name;

    if (argc > 1) {
        list_all_intf = false;
        interface_name = argv[1];
    }

    if (list_all_intf) {
        ds_put_cstr(ds, "================ Interfaces ================\n");
    } else {
        ds_put_format(ds, "================ Interface %s ================\n",
            interface_name);
    }

    SHASH_FOR_EACH(sh_node, &all_interfaces) {
        struct iface *intf = sh_node->data;
        if (list_all_intf
            || (!strcmp(interface_name, sh_node->name))) {

            ds_put_format(ds, "Interface %s:\n", intf->name);
            ds_put_format(ds, "    admin              : %d\n",
                          intf->user_cfg.admin_state);
            ds_put_format(ds, "    hw_enable          : %d\n",
                          intf->op_state.enabled);
            ds_put_format(ds, "    op_state_reason    : %s\n",
                          intfd_get_error_str(intf->op_state.reason));
            ds_put_format(ds, "    cfg_autoneg        : %s\n",
                          iface_config_autoneg_strings[intf->user_cfg.autoneg]);
            ds_put_format(ds, "    op_autoneg         : %d\n",
                          intf->op_state.autoneg);
            ds_put_format(ds, "    cfg_speeds         : %d\n",
                          intf->user_cfg.speeds);
            ds_put_format(ds, "    fixed_speed        : %u\n",
                          intf->op_state.fixed_speed);
            ds_put_format(ds, "    cfg_mtu            : %d\n",
                          intf->user_cfg.mtu);
            ds_put_format(ds, "    cfg_pause          : %d\n",
                          intf->user_cfg.pause);
            ds_put_format(ds, "    cfg_duplex         : %d\n",
                          intf->user_cfg.duplex);
            ds_put_format(ds, "    op_connector       : %s\n",
                          interface_pm_info_connector_strings[intf->pm_info.connector]);
            ds_put_format(ds, "    hw_interface_type  : %s\n",
                          intfd_get_intf_type_str(intf->pm_info.intf_type));
        }
    }

} /* intfd_debug_dump */

static uint64_t
get_connector_flags(enum ovsrec_interface_pm_info_connector_e connector)
{
    switch (connector) {
    case INTERFACE_PM_INFO_CONNECTOR_SFP_RJ45:
    case INTERFACE_PM_INFO_CONNECTOR_SFP_SX:
        return PM_SFP_FLAGS;
        break;
    case INTERFACE_PM_INFO_CONNECTOR_SFP_SR:
    case INTERFACE_PM_INFO_CONNECTOR_SFP_LR:
    case INTERFACE_PM_INFO_CONNECTOR_SFP_DAC:
        return PM_SFP_PLUS_FLAGS;
        break;
    case INTERFACE_PM_INFO_CONNECTOR_QSFP_CR4:
    case INTERFACE_PM_INFO_CONNECTOR_QSFP_SR4:
    case INTERFACE_PM_INFO_CONNECTOR_QSFP_LR4:
        return PM_QSFP_PLUS_40G_FLAGS;
        break;
    case INTERFACE_PM_INFO_CONNECTOR_ABSENT:
    case INTERFACE_PM_INFO_CONNECTOR_UNKNOWN:
    case INTERFACE_PM_INFO_CONNECTOR_SFP_LX:
    case INTERFACE_PM_INFO_CONNECTOR_SFP_CX:
    case INTERFACE_PM_INFO_CONNECTOR_SFP_FC:
    case INTERFACE_PM_INFO_CONNECTOR_SFP_LRM:
    default:
        return PM_UNSUPPORTED_FLAG;
        break;
    }
} /* get_connector_flags */

static enum ovsrec_interface_hw_intf_config_interface_type_e
get_connector_if_type(enum ovsrec_interface_pm_info_connector_e connector)
{
    switch (connector) {
    case INTERFACE_PM_INFO_CONNECTOR_SFP_SX:
        return INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_1GBASE_SX;
        break;
    case INTERFACE_PM_INFO_CONNECTOR_SFP_RJ45:
        return INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_1GBASE_T;
        break;
    case INTERFACE_PM_INFO_CONNECTOR_SFP_DAC:
        return INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_10GBASE_CR;
        break;
    case INTERFACE_PM_INFO_CONNECTOR_SFP_SR:
        return INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_10GBASE_SR;
        break;
    case INTERFACE_PM_INFO_CONNECTOR_SFP_LR:
        return INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_10GBASE_LR;
        break;
    case INTERFACE_PM_INFO_CONNECTOR_QSFP_CR4:
        return INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_40GBASE_CR4;
        break;
    case INTERFACE_PM_INFO_CONNECTOR_QSFP_SR4:
        return INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_40GBASE_SR4;
        break;
    case INTERFACE_PM_INFO_CONNECTOR_QSFP_LR4:
        return INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_40GBASE_LR4;
        break;
    default:
        return INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_UNKNOWN;
        break;
    }
} /* get_connector_if_type */

/* Create a connection to the OVSDB at db_path and create a dB cache
 * for this daemon. */
void
intfd_ovsdb_init(const char *db_path)
{
    /* Initialize IDL through a new connection to the dB. */
    idl = ovsdb_idl_create(db_path, &ovsrec_idl_class, false, true);
    idl_seqno = ovsdb_idl_get_seqno(idl);
    ovsdb_idl_set_lock(idl, "halon_intfd");

    /* Reject writes to columns which are not marked write-only using
     * ovsdb_idl_omit_alert(). */
    ovsdb_idl_verify_write_only(idl);

    /* Choose some OVSDB tables and columns to cache. */
    ovsdb_idl_add_table(idl, &ovsrec_table_open_vswitch);
    ovsdb_idl_add_table(idl, &ovsrec_table_interface);

    /* Monitor the following columns, marking them read-only. */
    ovsdb_idl_add_column(idl, &ovsrec_open_vswitch_col_cur_cfg);
    ovsdb_idl_add_column(idl, &ovsrec_interface_col_name);
    ovsdb_idl_add_column(idl, &ovsrec_interface_col_user_config);
    ovsdb_idl_add_column(idl, &ovsrec_interface_col_pm_info);

    /* Mark the following columns write-only. */
    ovsdb_idl_add_column(idl, &ovsrec_interface_col_error);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_error);

    ovsdb_idl_add_column(idl, &ovsrec_interface_col_hw_intf_config);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_hw_intf_config);

} /* intfd_ovsdb_init */

void
intfd_ovsdb_exit(void)
{
    struct shash_node *sh_node = NULL, *sh_next = NULL;

    SHASH_FOR_EACH_SAFE(sh_node, sh_next, &all_interfaces) {
        del_old_interface(sh_node);
    }
    ovsdb_idl_destroy(idl);
} /* intfd_ovsdb_exit */

static void
intfd_parse_user_cfg(struct intf_user_cfg *user_config,
                     const struct smap *ifrow_config)
{
    const char *data = NULL;

    intfd_print_smap("interface_user_config", ifrow_config);

    /* HALON_TODO: Add functions to validate the user_config data.
     * Without meta-schema we can't do such validation. */

    /* user_config:admin_state */
    user_config->admin_state = INTERFACE_USER_CONFIG_ADMIN_DOWN;

    data = smap_get(ifrow_config, INTERFACE_USER_CONFIG_MAP_ADMIN);
    if (data && (STR_EQ(data, OVSREC_INTERFACE_USER_CONFIG_ADMIN_UP))) {
        user_config->admin_state = INTERFACE_USER_CONFIG_ADMIN_UP;
    }

    /* user_config:autoneg */
    user_config->autoneg = INTERFACE_USER_CONFIG_AUTONEG_DEFAULT;

    data = smap_get(ifrow_config, INTERFACE_USER_CONFIG_MAP_AUTONEG);
    if (data && (STR_EQ(data, INTERFACE_USER_CONFIG_MAP_AUTONEG_OFF))) {
        user_config->autoneg = INTERFACE_USER_CONFIG_AUTONEG_OFF;

    } else if (data && (STR_EQ(data, INTERFACE_USER_CONFIG_MAP_AUTONEG_ON))) {
        user_config->autoneg = INTERFACE_USER_CONFIG_AUTONEG_ON;
    }

    /* user_config:pause */
    user_config->pause = INTERFACE_USER_CONFIG_PAUSE_NONE;

    data = smap_get(ifrow_config, INTERFACE_USER_CONFIG_MAP_PAUSE);
    if (data && (STR_EQ(data, INTERFACE_USER_CONFIG_MAP_PAUSE_RXTX))) {
        user_config->pause = INTERFACE_USER_CONFIG_PAUSE_RXTX;

    } else if (data && (STR_EQ(data, INTERFACE_USER_CONFIG_MAP_PAUSE_TX))) {
        user_config->pause = INTERFACE_USER_CONFIG_PAUSE_TX;

    } else if (data && (STR_EQ(data, INTERFACE_USER_CONFIG_MAP_PAUSE_RX))) {
        user_config->pause = INTERFACE_USER_CONFIG_PAUSE_RX;
    }

    /* user_config:duplex */
    user_config->duplex = INTERFACE_USER_CONFIG_DUPLEX_FULL;

    data = smap_get(ifrow_config, INTERFACE_USER_CONFIG_MAP_DUPLEX);
    if (data && (STR_EQ(data, INTERFACE_USER_CONFIG_MAP_DUPLEX_HALF))) {
        user_config->duplex = INTERFACE_USER_CONFIG_DUPLEX_HALF;
    }

    /* HALON_TODO: Enhance the code to take multiple speeds,
     * which can be passed on to vswitchd to do autoneg. */
    user_config->speeds = 0;
    data = smap_get(ifrow_config, INTERFACE_USER_CONFIG_MAP_SPEEDS);
    if (data) {
        user_config->speeds = atoi(data);
    }

    user_config->mtu = 0;
    data = smap_get(ifrow_config, INTERFACE_USER_CONFIG_MAP_MTU);
    if (data) {
        user_config->mtu = atoi(data);
    }

} /* intfd_parse_user_cfg */

static void
intfd_parse_pm_info(struct intf_pm_info *pm_info, const struct smap *ifrow_pm_info)
{
    const char *data = NULL;

    /* pm_info:connector_status */
    pm_info->connector_status = INTERFACE_PM_INFO_CONNECTOR_STATUS_UNRECOGNIZED;

    data = smap_get(ifrow_pm_info, INTERFACE_PM_INFO_MAP_CONNECTOR_STATUS);
    if (data && (STR_EQ(data, OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_UNSUPPORTED))) {
        pm_info->connector_status = INTERFACE_PM_INFO_CONNECTOR_STATUS_UNSUPPORTED;

    } else if (data && (STR_EQ(data, OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED))) {
        pm_info->connector_status = INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED;
    }

    if (pm_info->connector_status == INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED) {

        /* pm_info:connector */
        pm_info->connector = INTERFACE_PM_INFO_CONNECTOR_UNKNOWN;

        data = smap_get(ifrow_pm_info, INTERFACE_PM_INFO_MAP_CONNECTOR);
        if (data && (STR_EQ(data, OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP_CR4))) {
            pm_info->connector = INTERFACE_PM_INFO_CONNECTOR_QSFP_CR4;

        } else if (data && (STR_EQ(data, OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP_LR4))) {
            pm_info->connector = INTERFACE_PM_INFO_CONNECTOR_QSFP_LR4;

        } else if (data && (STR_EQ(data, OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP_SR4))) {
            pm_info->connector = INTERFACE_PM_INFO_CONNECTOR_QSFP_SR4;

        } else if (data && (STR_EQ(data, OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_CX))) {
            pm_info->connector = INTERFACE_PM_INFO_CONNECTOR_SFP_CX;

        } else if (data && (STR_EQ(data, OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_DAC))) {
            pm_info->connector = INTERFACE_PM_INFO_CONNECTOR_SFP_DAC;

        } else if (data && (STR_EQ(data, OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_FC))) {
            pm_info->connector = INTERFACE_PM_INFO_CONNECTOR_SFP_FC;

        } else if (data && (STR_EQ(data, OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_LR))) {
            pm_info->connector = INTERFACE_PM_INFO_CONNECTOR_SFP_LR;

        } else if (data && (STR_EQ(data, OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_LRM))) {
            pm_info->connector = INTERFACE_PM_INFO_CONNECTOR_SFP_LRM;

        } else if (data && (STR_EQ(data, OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_LX))) {
            pm_info->connector = INTERFACE_PM_INFO_CONNECTOR_SFP_LX;

        } else if (data && (STR_EQ(data, OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_RJ45))) {
            pm_info->connector = INTERFACE_PM_INFO_CONNECTOR_SFP_RJ45;

        } else if (data && (STR_EQ(data, OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_SR))) {
            pm_info->connector = INTERFACE_PM_INFO_CONNECTOR_SFP_SR;

        } else if (data && (STR_EQ(data, OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_SX))) {
            pm_info->connector = INTERFACE_PM_INFO_CONNECTOR_SFP_SX;

        } else if (data && (STR_EQ(data, OVSREC_INTERFACE_PM_INFO_CONNECTOR_ABSENT))) {
            pm_info->connector = INTERFACE_PM_INFO_CONNECTOR_ABSENT;
        }

        pm_info->op_connector_flags = get_connector_flags(pm_info->connector);
        pm_info->intf_type = get_connector_if_type(pm_info->connector);
    } else {
        pm_info->connector = INTERFACE_PM_INFO_CONNECTOR_ABSENT;
        pm_info->op_connector_flags = PM_UNSUPPORTED_FLAG;
        pm_info->intf_type = INTERFACE_HW_INTF_CONFIG_INTERFACE_TYPE_UNKNOWN;
    }
} /* intfd_parse_pm_info */

static void
set_op_state_pause(struct iface *intf)
{
    enum ovsrec_interface_hw_intf_config_pause_e hw_pause;

    switch(intf->user_cfg.pause) {

    case INTERFACE_USER_CONFIG_PAUSE_RXTX:
        hw_pause = INTERFACE_HW_INTF_CONFIG_PAUSE_RXTX;
        break;

    case INTERFACE_USER_CONFIG_PAUSE_TX:
        hw_pause = INTERFACE_HW_INTF_CONFIG_PAUSE_TX;
        break;

    case INTERFACE_USER_CONFIG_PAUSE_RX:
        hw_pause = INTERFACE_HW_INTF_CONFIG_PAUSE_RX;
        break;

    case INTERFACE_USER_CONFIG_PAUSE_NONE:
    default:
        hw_pause = INTERFACE_HW_INTF_CONFIG_PAUSE_NONE;
        break;
    }
    intf->op_state.pause = hw_pause;

} /* set_op_state_pause */

static void
set_op_state_duplex(struct iface *intf)
{
    enum ovsrec_interface_hw_intf_config_duplex_e hw_duplex;

    switch(intf->user_cfg.duplex) {

    case INTERFACE_USER_CONFIG_DUPLEX_HALF:
        hw_duplex = INTERFACE_HW_INTF_CONFIG_DUPLEX_HALF;
        break;

    case INTERFACE_USER_CONFIG_DUPLEX_FULL:
    default:
       hw_duplex = INTERFACE_HW_INTF_CONFIG_DUPLEX_FULL;
        break;
    }
    intf->op_state.duplex = hw_duplex;

} /* set_op_state_duplex */

static void
add_new_interface(const struct ovsrec_interface *ifrow)
{
    struct iface *new_intf = NULL;

    VLOG_DBG("Interface %s being added!\n", ifrow->name);

    /* If the interface already exists, return. */
    if (NULL != shash_find(&all_interfaces, ifrow->name)) {
        VLOG_WARN("Interface %s specified twice", ifrow->name);
        return;
    }

    /* Allocate structure to save state information for this interface. */
    new_intf = xzalloc(sizeof *new_intf);

    shash_add(&all_interfaces, ifrow->name, new_intf);

    new_intf->name = xstrdup(ifrow->name);

    intfd_parse_user_cfg(&new_intf->user_cfg, &ifrow->user_config);
    intfd_parse_pm_info(&new_intf->pm_info, &ifrow->pm_info);

    VLOG_DBG("Created local data structure for interface %s", ifrow->name);

} /* add_new_interface */

static void
del_old_interface(struct shash_node *sh_node)
{
    if (sh_node) {
        struct iface *intf = sh_node->data;
        free(intf->name);
        free(intf);
        shash_delete(&all_interfaces, sh_node);
    }
} /* del_old_interface */

/* FUNCTION: calc_intf_op_state_n_reason()
 *
 * This function determines an interface's operational state & reasons:
 *
 *    - op state:        Interface "hw_intf_config:enable"
 *    - op state reason: Interface "error"
 *
 * Following is a complete summary of the different operational states
 * and the associated reasons for an interface, listed in order of
 * priority, with the highest priority values listed first.
 *
 * Note that if multiple reasons apply to an interface, only the
 * highest priority reason is displayed.  E.g., if a pluggable
 * interface does not have any pluggable module installed, and its
 * interface admin state is set to disabled by an administrator, then
 * "error" will only show "admin_down".  It becomes "unpopulated"
 * after its admin interface state is set to enabled.
 *
 * OP STATE  OP STATE REASON        NOTES
 * --------  ---------------        -----
 *
 * disabled  lanes_split
 *                  Interface is not usable due to the interface being
 *                  configured in split mode.  Applicable to
 *                  splittable primary interface only (e.g. QSFP+).
 *
 * disabled  lanes_not_split
 *                  Interface is not usable due to the interface being
 *                  configured in non-split mode. Applicable to
 *                  splittable subinterfaces only (e.g. QSFP+).
 *
 * disabled  admin_down
 *                  Interface "user_config:admin" property is set to
 *                  "down" by an administrator.
 *
 * disabled  module_missing
 *                  Interface is pluggable, but no pluggable module is
 *                  present.
 *
 * disabled  module_unrecognized
 *                  Interface is pluggable, but the pluggable module
 *                  identification information cannot be read.
 *
 * disabled  module_unsupported
 *                  Interface is pluggable, but the pluggable module
 *                  type is not supported.  This may be one of the
 *                  following reasons:
 *                   1. Unsupported module.
 *                   2. Enet 1/10/40G modules in an interface that
 *                      does not support that particular Ethernet
 *                      speed.
 *
 * NOTE: All new checks should be added above the following
 *
 * enabled   ok
 *                  A supported module is present and everything is
 *                  fine.  Interface is enabled.  This does not imply
 *                  link is necessarily up; it simply means a
 *                  interface is enabled at the h/w level.
 */
static void
calc_intf_op_state_n_reason(struct iface *intf)
{
    VLOG_DBG("Checking interface %s in hardware.\n", intf->name);

    /* Default the interface to disabled. */
    intf->op_state.enabled = false;
    intf->op_state.reason = INTERFACE_ERROR_UNINITIALIZED;

    /* Checking admin state. */
    if (intf->user_cfg.admin_state ==  INTERFACE_USER_CONFIG_ADMIN_DOWN) {
        intf->op_state.reason = INTERFACE_ERROR_ADMIN_DOWN;

    /* Checking for missing pluggable module. */
    } else if (intf->pm_info.connector == INTERFACE_PM_INFO_CONNECTOR_ABSENT) {
        intf->op_state.reason = INTERFACE_ERROR_MODULE_MISSING;

    /* Checking for unrecognized pluggable module. */
    } else if (intf->pm_info.connector_status == INTERFACE_PM_INFO_CONNECTOR_STATUS_UNRECOGNIZED) {
        intf->op_state.reason = INTERFACE_ERROR_MODULE_UNRECOGNIZED;

    /* Checking for unsupported pluggable module. */
    } else if (intf->pm_info.connector_status == INTERFACE_PM_INFO_CONNECTOR_STATUS_UNSUPPORTED) {
        intf->op_state.reason = INTERFACE_ERROR_MODULE_UNSUPPORTED;

    } else {
        /* HALON_TODO: Lots of other business logic needs to be added here. */
        /* If we get here, everything's fine. */
        intf->op_state.enabled = true;
        intf->op_state.reason = INTERFACE_ERROR_OK;
        VLOG_DBG("Need to enable interface %s in hardware.\n", intf->name);
    }

} /* calc_intf_op_state_n_reason */

static void
set_interface_autoneg_capability(struct iface *intf)
{
    /* This function determines if an interface supports
     * auto-negotiation and the supported speed based on interface's
     * op_state.connector value.  Note that some interface types may support
     * auto-negotiation, but only of a particular fixed speed.  For
     * interfaces that don't support auto-negotiation, fixed_speed
     * must be non-0. */
    if (CONNECTOR_IS_SFP_PLUS(intf)) {
        intf->op_state.autoneg = false;
        intf->op_state.fixed_speed = SPEED_10G;

    } else if (CONNECTOR_IS_SFP(intf)) {
        intf->op_state.autoneg = true;
        intf->op_state.fixed_speed = SPEED_1G;

    } else if (CONNECTOR_IS_QSFP_PLUS_40G(intf)) {
        /* QSFP+ CR4 requires AN, SR4/LR4 do not. */
        if (INTERFACE_PM_INFO_CONNECTOR_QSFP_CR4 == intf->pm_info.connector) {
            intf->op_state.autoneg = true;
        } else {
            intf->op_state.autoneg = false;
        }
        intf->op_state.fixed_speed = SPEED_40G;

    } else {
        /* This should be midplane connectors.  As of now,
         * all midplane connections are of KR/KR2 variety,
         * which requires auto-negotiation, even if a
         * specific speed is specified later. */
        intf->op_state.autoneg = true;
        intf->op_state.fixed_speed = 0;
    }

    /* Override autoneg if user specified non-default setting. */
    switch (intf->user_cfg.autoneg) {
    case INTERFACE_USER_CONFIG_AUTONEG_ON:
        intf->op_state.autoneg = true;
        break;
    case INTERFACE_USER_CONFIG_AUTONEG_OFF:
        intf->op_state.autoneg = false;
        break;
    default:
        break;
    }

} /* set_interface_autoneg_capability */

void
set_intf_hw_config_in_db(const struct ovsrec_interface *ifrow, struct iface *intf)
{
    const char *tmp_str = NULL;

    struct smap smap = SMAP_INITIALIZER(&smap);

    /* Write H/W config changes to the interface row in OVSDB. */
    tmp_str = NULL;
    if (intf->op_state.enabled != true) {
        tmp_str = intfd_get_error_str(intf->op_state.reason);
    }
    ovsrec_interface_set_error(ifrow, tmp_str);

    /* We want to build up a new hw_intf_config map. */

    /* hw_intf_config:enabled */
    tmp_str = INTERFACE_HW_INTF_CONFIG_MAP_ENABLE_FALSE;
    if (intf->op_state.enabled == true) {
        tmp_str = INTERFACE_HW_INTF_CONFIG_MAP_ENABLE_TRUE;
    }

    smap_add(&smap, INTERFACE_HW_INTF_CONFIG_MAP_ENABLE, tmp_str);

    if (intf->op_state.enabled == true) {

        /* hw_intf_config:autoneg */
        tmp_str = INTERFACE_HW_INTF_CONFIG_MAP_AUTONEG_OFF;
        if (intf->op_state.autoneg == true) {
            tmp_str = INTERFACE_HW_INTF_CONFIG_MAP_AUTONEG_ON;
        }

        smap_add(&smap, INTERFACE_HW_INTF_CONFIG_MAP_AUTONEG, tmp_str);

        /* hw_intf_config:duplex */
        tmp_str = INTERFACE_HW_INTF_CONFIG_MAP_DUPLEX_FULL;
        if (intf->op_state.duplex == INTERFACE_HW_INTF_CONFIG_DUPLEX_HALF) {
            tmp_str = INTERFACE_HW_INTF_CONFIG_MAP_DUPLEX_HALF;
        }

        smap_add(&smap, INTERFACE_HW_INTF_CONFIG_MAP_DUPLEX, tmp_str);

        /* hw_intf_config:pause */
        tmp_str = INTERFACE_HW_INTF_CONFIG_MAP_PAUSE_NONE;
        if (intf->op_state.pause == INTERFACE_HW_INTF_CONFIG_PAUSE_RXTX) {
            tmp_str = INTERFACE_HW_INTF_CONFIG_MAP_PAUSE_RXTX;

        } else if (intf->op_state.pause == INTERFACE_HW_INTF_CONFIG_PAUSE_TX) {
            tmp_str = INTERFACE_HW_INTF_CONFIG_MAP_PAUSE_TX;

        } else if (intf->op_state.pause == INTERFACE_HW_INTF_CONFIG_PAUSE_RX) {
            tmp_str = INTERFACE_HW_INTF_CONFIG_MAP_PAUSE_RX;
        }

        smap_add(&smap, INTERFACE_HW_INTF_CONFIG_MAP_PAUSE, tmp_str);

        /* hw_intf_config:mtu */
        if (intf->op_state.mtu > 0) {
            smap_add_format(&smap, INTERFACE_HW_INTF_CONFIG_MAP_MTU, "%d", intf->op_state.mtu);
        }

        /* If fixed_speed is non-zero based on the
         * set_interface_autoneg_capability() function above, it is the
         * single speed supported by a given interface connector type,
         * typically pluggable types such as SFP/QSFP/etc.
         * In these cases, we ignore user-configured speeds. */
        if (0 != intf->op_state.fixed_speed) {
            smap_add_format(&smap, INTERFACE_HW_INTF_CONFIG_MAP_SPEEDS,
                            "%d", intf->op_state.fixed_speed);
        } else if (intf->op_state.speeds > 0) {
            /* Use user-configured speeds. */
            smap_add_format(&smap, INTERFACE_HW_INTF_CONFIG_MAP_SPEEDS,
                            "%d", intf->op_state.speeds);
        }

        smap_add(&smap, INTERFACE_HW_INTF_CONFIG_MAP_INTERFACE_TYPE,
                 intfd_get_intf_type_str(intf->pm_info.intf_type));
    }

    ovsrec_interface_set_hw_intf_config(ifrow, &smap);

} /* set_intf_hw_config_in_db */

static void
set_interface_config(const struct ovsrec_interface *ifrow, struct iface *intf)
{

    VLOG_DBG("Received new config for interface %s", ifrow->name);

    /* Figure out if intf can be enabled. */
    calc_intf_op_state_n_reason(intf);

    if (intf->op_state.enabled == true) {

        /* Update autoneg capabilities of the interface. */
        set_interface_autoneg_capability(intf);

        set_op_state_pause(intf);

        set_op_state_duplex(intf);

        intf->op_state.speeds = intf->user_cfg.speeds;
        intf->op_state.mtu = intf->user_cfg.mtu;
    }

    /* One interface needs to be reconfigured in h/w. */
    set_intf_hw_config_in_db(ifrow, intf);

} /* set_interface_config */

static int
handle_interfaces_config_mods(struct shash *sh_idl_interfaces)
{
    int rc = 0;
    bool cfg_changed = false;
    struct intf_user_cfg new_user_cfg;
    struct intf_pm_info new_pm_info;
    struct shash_node *sh_node;
    struct iface *intf = NULL;
    const struct ovsrec_interface *ifrow = NULL;

    /* Loop through all the current interfaces and figure out how many
     * have config changes that need action. */
    SHASH_FOR_EACH(sh_node, &all_interfaces) {

        cfg_changed = false;
        intf = sh_node->data;
        ifrow = shash_find_data(sh_idl_interfaces, sh_node->name);

        if (OVSREC_IDL_IS_ROW_INSERTED(ifrow, idl_seqno)) {

            set_interface_config(ifrow, intf);
            rc++;

        } else if (OVSREC_IDL_IS_ROW_MODIFIED(ifrow, idl_seqno)) {

            intfd_parse_user_cfg(&new_user_cfg, &ifrow->user_config);
            intfd_parse_pm_info(&new_pm_info, &ifrow->pm_info);

            if (intf->user_cfg.admin_state != new_user_cfg.admin_state) {
                cfg_changed = true;
                intf->user_cfg.admin_state = new_user_cfg.admin_state;
            }

            if (intf->user_cfg.autoneg != new_user_cfg.autoneg) {
                cfg_changed = true;
                intf->user_cfg.autoneg = new_user_cfg.autoneg;
            }

            if (intf->user_cfg.pause != new_user_cfg.pause) {
                cfg_changed = true;
                intf->user_cfg.pause = new_user_cfg.pause;
            }

            if (intf->user_cfg.duplex != new_user_cfg.duplex) {
                cfg_changed = true;
                intf->user_cfg.duplex = new_user_cfg.duplex;
            }

            if (intf->user_cfg.mtu != new_user_cfg.mtu) {
                cfg_changed = true;
                intf->user_cfg.mtu = new_user_cfg.mtu;
            }

            if (intf->user_cfg.speeds != new_user_cfg.speeds) {
                cfg_changed = true;
                intf->user_cfg.speeds = new_user_cfg.speeds;
            }

            if (intf->pm_info.connector != new_pm_info.connector) {
                cfg_changed = true;
                intf->pm_info.connector = new_pm_info.connector;
            }

            if (intf->pm_info.connector_status != new_pm_info.connector_status) {
                cfg_changed = true;
                intf->pm_info.connector_status = new_pm_info.connector_status;
            }

            if (intf->pm_info.intf_type != new_pm_info.intf_type) {
                cfg_changed = true;
                intf->pm_info.intf_type = new_pm_info.intf_type;
            }

            if (intf->pm_info.op_connector_flags != new_pm_info.op_connector_flags) {
                cfg_changed = true;
                intf->pm_info.op_connector_flags = new_pm_info.op_connector_flags;
            }

            if (cfg_changed) {
                /* Update interface configuration. */
                set_interface_config(ifrow, intf);
                rc++;
            }
        }
    }

    return rc;

} /* handle_interfaces_config_mods */

static int
intfd_reconfigure(void)
{
    int rc = 0;
    const struct ovsrec_interface *ifrow = NULL;
    unsigned int new_idl_seqno = 0;
    struct shash sh_idl_interfaces;
    struct shash_node *sh_node = NULL, *sh_next = NULL;

    new_idl_seqno = ovsdb_idl_get_seqno(idl);
    if (new_idl_seqno == idl_seqno) {
        /* There was no change in the dB. */
        return 0;
    }

    /* Collect all the interfaces in the dB. */
    shash_init(&sh_idl_interfaces);
    OVSREC_INTERFACE_FOR_EACH(ifrow, idl) {
        if (!shash_add_once(&sh_idl_interfaces, ifrow->name, ifrow)) {
            VLOG_WARN("interface %s specified twice", ifrow->name);
        }
    }

    /* Delete old interfaces.
     * Use SHASH_FOR_EACH_SAFE since del_old_interface()
     * will delete the current node. */
    SHASH_FOR_EACH_SAFE(sh_node, sh_next, &all_interfaces) {
        struct iface *new_intf = shash_find_data(&sh_idl_interfaces, sh_node->name);
        if (!new_intf) {
            VLOG_DBG("Deleting interface %s", sh_node->name);
            del_old_interface(sh_node);
        }
    }

    /* Add new interfaces. */
    SHASH_FOR_EACH(sh_node, &sh_idl_interfaces) {
        struct iface *new_intf = shash_find_data(&all_interfaces, sh_node->name);
        if (!new_intf) {
            VLOG_DBG("Adding new interface %s", sh_node->name);
            add_new_interface(sh_node->data);
        }
    }

    /* Check for number of interfaces that changed config--and need
     * handling now. */
    rc = handle_interfaces_config_mods(&sh_idl_interfaces);

    /* Update idl_seqno after handling all OVSDB updates. */
    idl_seqno = new_idl_seqno;

    /* Destroy the shash of the IDL interfaces */
    shash_destroy(&sh_idl_interfaces);

    return rc;
} /* intfd_reconfigure */

static inline bool
intfd_system_is_configured(void)
{
    const struct ovsrec_open_vswitch *vswrow = NULL;

    if (system_configured) {
        return true;
    }

    vswrow = ovsrec_open_vswitch_first(idl);

    if (vswrow && vswrow->cur_cfg > INT64_C(0)) {
        VLOG_DBG("System now configured (cur_cfg=%" PRId64 ").",
                 vswrow->cur_cfg);
        return (system_configured = true);
    }

    return false;
} /* intfd_system_is_configured */

void
intfd_run(void)
{
    struct ovsdb_idl_txn *txn;

    /* Process a batch of messages from OVSDB. */
    ovsdb_idl_run(idl);

    if (ovsdb_idl_is_lock_contended(idl)) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 1);

        VLOG_ERR_RL(&rl, "Another intfd process is running, "
                    "disabling this process until it goes away");

        return;
    } else if (!ovsdb_idl_has_lock(idl)) {
        return;
    }

    /* Nothing to do until system has been configured, i.e., cur_cfg > 0. */
    if (!intfd_system_is_configured()) {
        return;
    }

    /* Update the local configuration and push any changes to the dB. */
    txn = ovsdb_idl_txn_create(idl);
    if (intfd_reconfigure()) {
        /* Some OVSDB write needs to happen. */
        ovsdb_idl_txn_commit_block(txn);
    }
    ovsdb_idl_txn_destroy(txn);

    return;
} /* intfd_run */

void
intfd_wait(void)
{
    ovsdb_idl_wait(idl);
} /* intfd_wait */

/** @} end of group intfd */
