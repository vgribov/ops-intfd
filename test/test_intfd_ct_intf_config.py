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

def short_sleep(tm=.5):
    time.sleep(tm)

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
        s1.cmd("/usr/bin/ovs-vsctl set interface 1 user_config:admin=up")
        short_sleep()

        # Verify that 'error' column in interface table is
        # set to 'module_missing' due to absense of pluggable modules.
        # Verify that hw_intf_config:enable is still set to 'false'.
        info("Verify that 'error' is set to 'module_missing'\n")
        out = s1.ovscmd("/usr/bin/ovs-vsctl get interface 1 error hw_intf_config:enable")
        err, hw_enable = out.splitlines()
        if err != 'module_missing' or hw_enable != 'false':
            assert 0, "Expected the interface " \
                      "'error' status to be 'module_missing', and " \
                      "'hw_intf_config:enable' status to be 'false', " \
                      "but they are error = %s, hw_intf_config:enable = %s." \
                      % (err, hw_enable)

        # Set admin state as 'down' for interface 1.
        s1.cmd("/usr/bin/ovs-vsctl set interface 1 user_config:admin=down")
        short_sleep()

        # Verify that 'error' column in interface table is
        # set to 'admin_down' as port is disabled by user.
        # Verify that hw_intf_config:enable is still set to 'false'.
        info("Verify that 'error' is set to 'admin_down' when admin=down without other params set\n")
        out = s1.ovscmd("/usr/bin/ovs-vsctl get interface 1 error hw_intf_config:enable")
        err, hw_enable = out.splitlines()
        if err != 'admin_down' or hw_enable != 'false':
            assert 0, "Expected the interface " \
                      "'error' status to be 'admin_down', " \
                      "'hw_intf_config:enable' status to be 'false', " \
                      "but they are error = %s, hw_intf_config:enable = %s." \
                      % (err, hw_enable)

        # Set the complete user_config, but leave the admin=down,
        # verify that still interface hardware_intf_config:enable=false
        s1.cmd("/usr/bin/ovs-vsctl set interface 1 " \
               "user_config:admin=down user_config:autoneg=on " \
               "user_config:speeds=1000 user_config:duplex=full " \
               "user_config:pause=rxtx user_config:mtu=1500")
        short_sleep()

        # Verify that 'error' column in interface table is
        # set to 'admin_down' as port is disabled by user.
        # Verify that hw_intf_config:enable is still set to 'false'.
        info("Verify that 'error' is set to 'admin_down' when admin=down " \
             "and other params are set to valid values.\n")
        out = s1.ovscmd("/usr/bin/ovs-vsctl get interface 1 error hw_intf_config:enable")
        err, hw_enable = out.splitlines()
        if err != 'admin_down' or hw_enable != 'false':
            assert 0, "Expected the interface " \
                      "'error' status to be 'admin_down', " \
                      "'hw_intf_config:enable' status to be 'false', " \
                      "but they are error = %s, hw_intf_config:enable = %s." \
                      % (err, hw_enable)

        # clear the user_config of interface 1.
        s1.cmd("/usr/bin/ovs-vsctl clear interface 1 user_config")

        # Verify that interface is still disabled.
        out = s1.ovscmd("/usr/bin/ovs-vsctl get interface 1 error hw_intf_config:enable")
        err, hw_enable = out.splitlines()
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

        s1.cmd("/usr/bin/ovs-vsctl set interface 1 user_config:admin=up")

        # Set the pluggable module info to valid values.
        s1.cmd("/usr/bin/ovs-vsctl set interface 1 " \
               "pm_info:connector=SFP_RJ45 pm_info:connector_status=supported")
        short_sleep()

        # Make sure that 'error' status is not 'module_missing'
        info("Verify that interface pluggable module status is correctly detected.\n")
        out = s1.ovscmd("/usr/bin/ovs-vsctl list interface 1")
        if 'module_missing' == out:
            assert 0, "Expected the interface error column value is not equal to 'module_missing'"

        # Set pm_info:connector as 'absent'
        s1.cmd("/usr/bin/ovs-vsctl set interface 1 " \
               "pm_info:connector=absent pm_info:connector_status=unrecognized")
        short_sleep()

        # Make sure that 'error' status is set to 'module_missing'
        info("Verify that pluggable module removal is correctly detected.\n")
        out = s1.ovscmd("/usr/bin/ovs-vsctl get interface 1 error hw_intf_config:enable")
        err, hw_enable = out.splitlines()
        if err != 'module_missing' or hw_enable != 'false':
            assert 0, "Expected the interface " \
                      "'error' status to be 'module_missing', " \
                      "'hw_intf_config:enable' status to be 'false', " \
                      "but they are error = %s, hw_intf_config:enable = %s." \
                      % (err, hw_enable)

        # Set pm_info:connector as 'absent'
        s1.cmd("/usr/bin/ovs-vsctl set interface 1 " \
               "pm_info:connector=unknown pm_info:connector_status=unsupported")
        short_sleep()

        # Make sure that 'error' status is set to 'module_missing'
        info("Verify that pluggable module removal is correctly detected.\n")
        out = s1.ovscmd("/usr/bin/ovs-vsctl get interface 1 error hw_intf_config:enable")
        err, hw_enable = out.splitlines()
        if err != 'module_missing' or hw_enable != 'false':
            assert 0, "Expected the interface " \
                      "'error' status to be 'module_missing', " \
                      "'hw_intf_config:enable' status to be 'false', " \
                      "but they are error = %s, hw_intf_config:enable = %s." \
                      % (err, hw_enable)

        # clear the OVSDB config of the interface 1
        s1.cmd("/usr/bin/ovs-vsctl clear interface 1 user_config")
        s1.cmd("/usr/bin/ovs-vsctl set interface 1 " \
               "pm_info:connector=absent pm_info:connector_status=unsupported")


    def user_config_autoneg(self):

        info("\n============= intfd user config tests =============\n")
        s1 = self.net.switches[0]

        s1.cmd("/usr/bin/ovs-vsctl set interface 2 " \
               "user_config:admin=up user_config:autoneg=on")

        # Set the pluggable module to 'SFP_RJ45'
        s1.cmd("/usr/bin/ovs-vsctl set interface 2 " \
               "pm_info:connector=SFP_RJ45 pm_info:connector_status=supported")
        short_sleep()

        info("Verify that 'hw_intf_config:autoneg' is set to 'on' when connector=SFP_RJ45\n")
        out = s1.ovscmd("/usr/bin/ovs-vsctl get interface 2 hw_intf_config:autoneg")
        assert 'on' != out, "Autoneg configuration in hw_intf_config is wrong for SFP_RJ45 module"

        # HALON_TODO: SFP_PLUS autoneg is not calulated correctly by intfd.
        # Once that is fixed add more tests based on that.

        # clear the OVSDB config of the interface 2
        s1.cmd("/usr/bin/ovs-vsctl clear interface 2 user_config")
        s1.cmd("/usr/bin/ovs-vsctl set interface 2 " \
               "pm_info:connector=absent pm_info:connector_status=unsupported")

    def user_config_pause(self):

        pause_values = ('none', 'rx', 'tx', 'rxtx')

        info("\n============= intfd user config tests =============\n")
        s1 = self.net.switches[0]

        # Set user_config & pm_info to valid values.
        s1.cmd("/usr/bin/ovs-vsctl set interface 2 " \
               "pm_info:connector=SFP_RJ45 pm_info:connector_status=supported")
        s1.cmd("/usr/bin/ovs-vsctl set interface 2 " \
               "user_config:admin=up user_config:autoneg=on")
        short_sleep()

        for val in pause_values:
            info("Testing user_config:pause parameter with %s\n" % val)
            s1.cmd("/usr/bin/ovs-vsctl set interface 2 user_config:pause=%s" % val)
            short_sleep()

            out = s1.ovscmd("/usr/bin/ovs-vsctl get interface 2 hw_intf_config:pause")
            assert val != out, "pause configuration in hw_intf_config is wrong for pause=%s\n" % val

        # clear the OVSDB config of the interface 2
        s1.cmd("/usr/bin/ovs-vsctl clear interface 2 user_config")
        s1.cmd("/usr/bin/ovs-vsctl set interface 2 " \
               "pm_info:connector=absent pm_info:connector_status=unsupported")

    def user_config_speeds(self):
        # HALON_TODO: Fixed speed calculations based on the
        # pluggable module is not working as expected.
        # Once it is corrected, implement the tests based on that.
        pass

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
