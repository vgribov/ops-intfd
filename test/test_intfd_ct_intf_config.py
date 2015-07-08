#!/usr/bin/python
#
# Copyright (C) 2015 Hewlett-Packard Development Company, L.P.
# All Rights Reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.
#

import os
import sys
import time
import subprocess
import pytest

from halonvsi.docker import *
from halonvsi.halon import *

OVS_VSCTL = "/usr/bin/ovs-vsctl "

# In X86 config Interface 50 is Splittable port.
split_parent = '50'
split_children = ['50-1', '50-2', '50-3', '50-4']

def short_sleep(tm=.5):
    time.sleep(tm)


# Set user_config for a Interface.
def sw_set_intf_user_config(sw, interface, config):
    c = "/usr/bin/ovs-vsctl set interface " + str(interface)
    for s in config:
        c += " user_config:" + s
    debug(c)
    return sw.cmd(c)


# Clear user_config for a Interface.
def sw_clear_user_config(sw, interface):
    c = "/usr/bin/ovs-vsctl clear interface " + str(interface) + " user_config"
    debug(c)
    return sw.cmd(c)


# Set pm_info for a Interface.
def sw_set_intf_pm_info(sw, interface, config):
    c = "/usr/bin/ovs-vsctl set interface " + str(interface)
    for s in config:
        c += " pm_info:" + s
    debug(c)
    return sw.cmd(c)


# Get the values of a set of columns from Interface table.
# This function returns a list of values if 2 or more
# fields are requested, and returns a single value (no list)
# if only 1 field is requested.
def sw_get_intf_state(sw, interface, fields):
    c = "/usr/bin/ovs-vsctl get interface " + str(interface)
    for f in fields:
        c += " " + f
    out = sw.ovscmd(c).splitlines()
    # If a single column value is requested,
    # then return a singleton value instead of list.
    if len(out) == 1:
        out = out[0]
    debug(out)
    return out


class intfdTest(HalonTest):

    def setupNet(self):

        # Create a topology with single Halon switch and
        # one regular host.
        host_opts = self.getHostOpts()
        switch_opts = self.getSwitchOpts()
        intfd_topo = SingleSwitchTopo(k=1, hopts=host_opts, sopts=switch_opts)
        self.net = Mininet(intfd_topo, switch=HalonSwitch,
                           host=Host, link=HalonLink,
                           controller=None, build=True)
        self.switch1 = self.net.switches[0]


    def user_config(self):

        info("\n============= intfd user config tests =============\n")
        s1 = self.net.switches[0]

        info("Verify that interface '1' is present in the DB.\n")
        out = s1.cmd("/usr/bin/ovs-vsctl list interface 1")
        if '_uuid' not in out:
            info(out)
            assert 0, "Unable to find Interface '1' in OVSBD.\n"

        # Set admin state as 'up' for interface 1.
        sw_set_intf_user_config(s1, 1, ['admin=up'])
        short_sleep()

        # Verify that 'error' column in interface table is
        # set to 'module_missing' due to absense of pluggable module.
        # Verify that hw_intf_config:enable is still set to 'false'.
        info("Verify that 'error' is set to 'module_missing'\n")
        err, hw_enable = sw_get_intf_state(s1, 1, ['error', 'hw_intf_config:enable'])
        if err != 'module_missing' or hw_enable != 'false':
            assert 0, "Expected the interface " \
                      "'error' status to be 'module_missing', and " \
                      "'hw_intf_config:enable' status to be 'false', " \
                      "but they are error = %s, hw_intf_config:enable = %s." \
                      % (err, hw_enable)

        # Set admin state as 'down' for interface 1.
        sw_set_intf_user_config(s1, 1, ['admin=down'])
        short_sleep()

        # Verify that 'error' column in interface table is
        # set to 'admin_down' as port is disabled by user.
        # Verify that hw_intf_config:enable is still set to 'false'.
        info("Verify that 'error' is set to 'admin_down' when admin=down without other params set\n")
        err, hw_enable = sw_get_intf_state(s1, 1, ['error', 'hw_intf_config:enable'])
        if err != 'admin_down' or hw_enable != 'false':
            assert 0, "Expected the interface " \
                      "'error' status to be 'admin_down', " \
                      "'hw_intf_config:enable' status to be 'false', " \
                      "but they are error = %s, hw_intf_config:enable = %s." \
                      % (err, hw_enable)

        # Set the complete user_config, but leave the admin=down,
        # verify that still interface hardware_intf_config:enable=false
        sw_set_intf_user_config(s1, 1, ['admin=down', 'autoneg=on', 'speeds=1000', \
                                        'duplex=full', 'pause=rxtx', 'mtu=1500' ])
        short_sleep()

        # Verify that 'error' column in interface table is
        # set to 'admin_down' as port is disabled by user.
        # Verify that hw_intf_config:enable is still set to 'false'.
        info("Verify that 'error' is set to 'admin_down' when admin=down " \
             "and other params are set to valid values.\n")
        err, hw_enable = sw_get_intf_state(s1, 1, ['error', 'hw_intf_config:enable'])
        if err != 'admin_down' or hw_enable != 'false':
            assert 0, "Expected the interface " \
                      "'error' status to be 'admin_down', " \
                      "'hw_intf_config:enable' status to be 'false', " \
                      "but they are error = %s, hw_intf_config:enable = %s." \
                      % (err, hw_enable)

        # Clear the user_config of interface 1.
        sw_clear_user_config(s1, 1)

        # Verify that interface is still disabled.
        err, hw_enable = sw_get_intf_state(s1, 1, ['error', 'hw_intf_config:enable'])
        if err != 'admin_down' or hw_enable != 'false':
            assert 0, "Expected the interface " \
                      "'error' status to be 'admin_down', " \
                      "'hw_intf_config:enable' status to be 'false', " \
                      "but they are error = %s, hw_intf_config:enable = %s." \
                      % (err, hw_enable)


    # Set pm_info to valid values, and verify that 'intfd'
    # changes the "error" state to something other than 'module_missing'
    def pm_info_detect(self):

        info("\n============= intfd pm_info detect tests =============\n")
        s1 = self.net.switches[0]

        # Enable the interface 1, and set the pm_info to valid values.
        sw_set_intf_user_config(s1, 1, ['admin=up'])
        sw_set_intf_pm_info(s1, 1, ('connector=SFP_RJ45', 'connector_status=supported'))
        short_sleep()

        # Make sure that 'error' status is not 'module_missing'
        info("Verify that interface pluggable module status is correctly detected.\n")
        err = sw_get_intf_state(s1, 1, ['error'])
        if 'module_missing' == err:
            assert 0, "Expected the interface error column value is not equal to 'module_missing'"

        # Set pm_info:connector as 'absent'
        sw_set_intf_pm_info(s1, 1, ('connector=absent', 'connector_status=unrecognized'))
        short_sleep()

        # Make sure that 'error' status is set to 'module_missing'
        info("Verify that pluggable module removal is correctly detected.\n")
        err, hw_enable = sw_get_intf_state(s1, 1, ['error', 'hw_intf_config:enable'])
        if err != 'module_missing' or hw_enable != 'false':
            assert 0, "Expected the interface " \
                      "'error' status to be 'module_missing', " \
                      "'hw_intf_config:enable' status to be 'false', " \
                      "but they are error = %s, hw_intf_config:enable = %s." \
                      % (err, hw_enable)

        # Set pm_info:connector as 'unknown' and connector_status=unrecognized
        sw_set_intf_pm_info(s1, 1, ('connector=unknown', 'connector_status=unrecognized'))
        short_sleep()

        info("Verify that when connector_status=unrecognized, error is module_unrecognized.\n")
        err, hw_enable = sw_get_intf_state(s1, 1, ['error', 'hw_intf_config:enable'])
        assert err == 'module_unrecognized' and hw_enable == 'false', \
               "Invalid interface status when unrecognized pluggable module is inserted"

        # Set pm_info:connector as 'unknown' and connector_status=unsupported
        sw_set_intf_pm_info(s1, 1, ('connector=unknown', 'connector_status=unsupported'))
        short_sleep()

        info("Verify that when connector_status=unsupported, error is module_unsupported.\n")
        err, hw_enable = sw_get_intf_state(s1, 1, ['error', 'hw_intf_config:enable'])
        assert err == 'module_unsupported' and hw_enable == 'false', \
               "Invalid interface status when unsupported pluggable module is inserted"

        # Verify the hw_intf_config:interface_type based on 'pm_info:connector'
        pm_intf_type = {
            'SFP_RJ45' : '1GBASE_T',
            'SFP_SX' : '1GBASE_SX',
            'SFP_SR' : '10GBASE_SR',
            'SFP_LR' : '10GBASE_LR',
            'SFP_DAC' : '10GBASE_CR' }

        for pm_type,intf_type in pm_intf_type.items():
            sw_set_intf_pm_info(s1, 1, ['connector=' + pm_type, 'connector_status=supported'])
            short_sleep()

            info("Verify that 'hw_intf_config:interface_type' is set to " + intf_type + \
                 " when connector=" + pm_type + "\n")
            out = sw_get_intf_state(s1, 1, ['hw_intf_config:interface_type'])
            assert intf_type == out, "hw_intf_type configuration in hw_intf_config is wrong."

        # In X86 config Interface 50 is Splittable port.

        # Enable the split parent interface.
        sw_set_intf_user_config(s1, split_parent, ['admin=up', 'lane_split=no-split'])

        pm_intf_type = {
            'SFP_SR' : '10GBASE_SR',
            'SFP_LR' : '10GBASE_LR',
            'SFP_DAC' : '10GBASE_CR',
            'QSFP_CR4' : '40GBASE_CR4',
            'QSFP_SR4' : '40GBASE_SR4',
            'QSFP_LR4' : '40GBASE_LR4' }

        for pm_type, intf_type in pm_intf_type.items():
            sw_set_intf_pm_info(s1, split_parent, ['connector=' + pm_type, 'connector_status=supported'])
            short_sleep()

            info("Verify that 'hw_intf_config:interface_type' is set to " + intf_type + \
                 " when connector=" + pm_type + " on QSFP ports.\n")
            out = sw_get_intf_state(s1, split_parent, ['hw_intf_config:interface_type'])
            assert intf_type == out, "hw_intf_type configuration in hw_intf_config is wrong for split parent."

        # Enable the split children port.
        sw_set_intf_user_config(s1, split_parent, ['admin=down', 'lane_split=split'])
        for child_port in split_children:
            sw_set_intf_user_config(s1, child_port, ['admin=up']);

        # HW intf config for split children.
        pm_intf_type = {
            'QSFP_CR4' : '10GBASE_CR',
            'QSFP_SR4' : '10GBASE_SR',
            'QSFP_LR4' : '10GBASE_LR' }

        for pm_type, intf_type in pm_intf_type.items():
            sw_set_intf_pm_info(s1, split_parent, ['connector=' + pm_type, 'connector_status=supported'])
            short_sleep()

            info("Verify that 'hw_intf_config:interface_type' is set to " + intf_type + \
                 " when connector=" + pm_type + " on QSFP split ports.\n")
            for child_port in split_children:
                out = sw_get_intf_state(s1, child_port, ['hw_intf_config:interface_type'])
                assert intf_type == out, "hw_intf_type configuration in hw_intf_config is wrong for split child."

        # Unsupported pluggable modules when interface is split
        pm_intf_type = [ 'SFP_RJ45', 'SFP_SX', 'SFP_SR', 'SFP_LR', 'SFP_DAC' ]

        for pm_type in pm_intf_type:
            sw_set_intf_pm_info(s1, split_parent, ['connector=' + pm_type, 'connector_status=supported'])
            short_sleep()

            info("Verify that split port is disabled when unsupported module %s is inserted.\n" % pm_type)
            for child_port in split_children:
                err, hw_enable = sw_get_intf_state(s1, child_port, ['error', 'hw_intf_config:enable'])
                assert err == 'module_unsupported' and hw_enable == 'false', \
                       "Split children should not be enabled when non QSFP module is inserted."

        # Clear the OVSDB config of the interfaces
        sw_clear_user_config(s1, 1)
        sw_set_intf_pm_info(s1, 1, ('connector=absent', 'connector_status=unsupported'))

        sw_clear_user_config(s1, split_parent);
        sw_set_intf_pm_info(s1, split_parent, ('connector=absent', 'connector_status=unsupported'))

        for child_port in split_children:
            sw_clear_user_config(s1, child_port);


    def user_config_autoneg(self):

        info("\n============= intfd user config autoneg tests =============\n")
        s1 = self.net.switches[0]

        sw_set_intf_user_config(s1, 2, ['admin=up'])
        sw_set_intf_user_config(s1, split_parent, ['admin=up', 'lane_split=no-split'])

        pm_autoneg = {
            'SFP_RJ45' : 'on',
            'SFP_SX' : 'on',
            'SFP_SR' : 'off',
            'SFP_LR' : 'off',
            'SFP_DAC' : 'off' }

        for pm_type, autoneg in pm_autoneg.items():
            sw_set_intf_pm_info(s1, 2, ["connector=" + pm_type, 'connector_status=supported'])
            short_sleep()

            info("Verify that 'hw_intf_config:autoneg' is set to " + autoneg + \
                 " when connector=" + pm_type + "\n")
            out = sw_get_intf_state(s1, 2, ['hw_intf_config:autoneg'])
            assert autoneg == out, "Autoneg configuration in hw_intf_config is wrong."

        pm_autoneg = {
            'SFP_SR' : 'off',
            'SFP_LR' : 'off',
            'SFP_DAC' : 'off',
            'QSFP_CR4' : 'on',
            'QSFP_SR4' : 'off',
            'QSFP_LR4' : 'off' }

        for pm_type, autoneg in pm_autoneg.items():
            sw_set_intf_pm_info(s1, split_parent, ['connector=' + pm_type, 'connector_status=supported'])
            short_sleep()

            info("Verify that 'hw_intf_config:autoneg' is set to " + autoneg + \
                 " when connector=" + pm_type + " for split parent interface.\n")
            out = sw_get_intf_state(s1, split_parent, ['hw_intf_config:autoneg'])
            assert autoneg == out, "Autoneg configuration in hw_intf_config is wrong for split parent interface."

        # Enable the split children port.
        sw_set_intf_user_config(s1, split_parent, ['admin=down', 'lane_split=split'])
        for child_port in split_children:
            sw_set_intf_user_config(s1, child_port, ['admin=up']);

        # HW intf config for split children.
        pm_autoneg = {
            'QSFP_CR4' : 'off',
            'QSFP_SR4' : 'off',
            'QSFP_LR4' : 'off' }

        for pm_type, autoneg in pm_autoneg.items():
            sw_set_intf_pm_info(s1, split_parent, ['connector=' + pm_type, 'connector_status=supported'])
            short_sleep()

            info("Verify that 'hw_intf_config:autoneg' is set to " + autoneg + \
                 " when connector=" + pm_type + " on QSFP split ports.\n")
            for child_port in split_children:
                out = sw_get_intf_state(s1, child_port, ['hw_intf_config:autoneg'])
                assert autoneg == out, "Autgoneg configuration in hw_intf_config is wrong for split child."

        # clear the OVSDB config of the interface 2
        sw_clear_user_config(s1, 2)
        sw_set_intf_pm_info(s1, 2, ('connector=absent', 'connector_status=unsupported'))

        # clear the OVSDB config of the interface 50
        sw_clear_user_config(s1, split_parent)
        sw_set_intf_pm_info(s1, split_parent, ('connector=absent', 'connector_status=unsupported'))


    def user_config_pause(self):

        info("\n============= intfd user config pause tests =============\n")
        s1 = self.net.switches[0]

        # Set user_config & pm_info to valid values.
        sw_set_intf_user_config(s1, 2, ['admin=up'])
        sw_set_intf_pm_info(s1, 2, ('connector=SFP_RJ45', 'connector_status=supported'))
        short_sleep()

        pause_values = ('none', 'rx', 'tx', 'rxtx')

        for val in pause_values:
            info("Testing user_config:pause parameter with %s\n" % val)
            sw_set_intf_user_config(s1, 2, ["pause=%s" % val])
            short_sleep()

            out = sw_get_intf_state(s1, 2, ['hw_intf_config:pause'])
            assert val == out, "pause configuration in hw_intf_config is wrong for pause=%s\n" % val

        # Clear the OVSDB config of the interface 2
        sw_clear_user_config(s1, 2)
        sw_set_intf_pm_info(s1, 2, ('connector=absent', 'connector_status=unsupported'))


    def user_config_speeds(self):
        # HALON_TODO: Fixed speed calculations based on the
        # pluggable module is not working as expected.
        # Once it is corrected, implement the tests based on that.
        pass


    def user_config_qsfp_splitter(self):

        info("\n============= intfd user config QSFP splitter tests =============\n")
        s1 = self.net.switches[0]

        # Verify that the default state of parent interface is 'admin_down'
        info("Verify that default 'error' status for split parent is 'admin_down'\n")
        out = sw_get_intf_state(s1, split_parent, ['error'])
        assert 'admin_down' == out, "Initial status of QSFP split parent interface is incorrect."

        # Verify that the default state of children ports is 'lanes_not_split'
        info("Verify that 'error' status for split children is 'lanes_not_split'\n")
        for child_port in split_children:
            out = sw_get_intf_state(s1, child_port, ['error'])
            assert 'lanes_not_split' == out, "Initial status of QSFP split children port is incorrect."

        # Set Pluggable module info to valid values on parent interface.
        sw_set_intf_pm_info(s1, split_parent, ('connector=QSFP_CR4', 'connector_status=supported'))

        # Make sure that split children are disabled as long as
        # user_config:lane_split=no-split on the parent interface.
        sw_set_intf_user_config(s1, split_parent, ['admin=up', 'lane_split=no-split'])

        info("Verify that parent interface is enabled when 'lane_split=no-split'\n");
        hw_enable = sw_get_intf_state(s1, split_parent, ['hw_intf_config:enable'])
        assert hw_enable == 'true', "Invalid parent interface status. Expected the port to be up"

        # Enable the split children via user_config, but they should be still
        # disabled in the hw_intf_config.
        for child_port in split_children:
            sw_set_intf_user_config(s1, child_port, ['admin=up'])

        short_sleep()
        info("Verify that split children ports are disabled when 'lane_split=no-split'\n");
        for child_port in split_children:
            hw_enable = sw_get_intf_state(s1, child_port, ['hw_intf_config:enable'])
            assert hw_enable == 'false', "Invalid split child port status. Expected the port to be disabled"

        # Split the parent interface and make sure that parent interface is disabled, and
        # split child ports are enabled.
        sw_set_intf_user_config(s1, split_parent, ['admin=up', 'lane_split=split'])
        short_sleep()

        info("Verify that parent interface is disabled when 'lane_split=split'\n");
        err, hw_enable = sw_get_intf_state(s1, split_parent, ['error', 'hw_intf_config:enable'])
        assert err == 'lanes_split' and hw_enable == 'false', \
               "Invalid parent interface status. Expected the port to be down"

        info("Verify that split children ports are enabled when 'lane_split=split'\n");
        for child_port in split_children:
            hw_enable = sw_get_intf_state(s1, child_port, ['hw_intf_config:enable'])
            assert hw_enable == 'true', "Invalid split child port status. Expected the port to be enabled"

        info("Verify that disabling parent interface doesn't affect split ports when 'lane_split=split'\n");
        sw_set_intf_user_config(s1, split_parent, ['admin=down'])

        info("Verify that split children ports are enabled even if parent is disabled.\n");
        for child_port in split_children:
            hw_enable = sw_get_intf_state(s1, child_port, ['hw_intf_config:enable'])
            assert hw_enable == 'true', "Invalid split child port status. Expected the port to be enabled"

        # Change the lane_split to 'no-split', then all child ports should be disabled.
        info("Verify that all child ports are disabled when lane_split on parent interface is changed to no-split.\n")
        sw_set_intf_user_config(s1, split_parent, ['lane_split=no-split'])
        for child_port in split_children:
            hw_enable = sw_get_intf_state(s1, child_port, ['hw_intf_config:enable'])
            assert hw_enable == 'false', "Invalid split child port status. Expected the port to be disabled"

        # Cleanup
        sw_clear_user_config(s1, split_parent);
        sw_set_intf_pm_info(s1, split_parent, ('connector=absent', 'connector_status=unsupported'))

        for child_port in split_children:
            sw_clear_user_config(s1, child_port);


class Test_intfd:

    # Create the Mininet topology based on mininet.
    test = intfdTest()

    def setup(self):
        pass

    def teardown(self):
        pass

    def setup_class(cls):
        # Stop PMD. This tests manually sets lot of DB elements
        # that 'pmd' is responsible for. To avoid any cross
        # interaction disable 'pmd'
        Test_intfd.test.switch1.cmd("/bin/systemctl stop pmd")
        pass

    def teardown_class(cls):
        Test_intfd.test.switch1.cmd("/bin/systemctl start pmd")
        # Stop the Docker containers, and
        # mininet topology
        Test_intfd.test.net.stop()

    def setup_method(self, method):
        pass

    def teardown_method(self, method):
        pass

    def __del__(self):
        del self.test

    # Interface daemon tests.
    def test_intfd_user_config(self):
        self.test.user_config()

    def test_intfd_pm_info(self):
        self.test.pm_info_detect()

    def test_intfd_user_config_autoneg(self):
        self.test.user_config_autoneg()

    def test_intfd_user_config_pause(self):
        self.test.user_config_pause()

    def test_intfd_user_config_speeds(self):
        self.test.user_config_speeds()

    def test_intfd_user_config_qsfp_splitter(self):
        self.test.user_config_qsfp_splitter()
