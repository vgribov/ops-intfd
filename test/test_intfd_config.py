#!/usr/bin/python

import os
import sys
import time
import pytest
import subprocess
from halonvsi.docker import *
from halonvsi.halon import *

OVS_VSCTL = "/usr/bin/ovs-vsctl "

class intfdTest( HalonTest ):

    def setupNet(self):
        # if you override this function, make sure to
        # either pass getNodeOpts() into hopts/sopts of the topology that
        # you build or into addHost/addSwitch calls
        self.net = Mininet(topo=SingleSwitchTopo(
            k=1,
            hopts=self.getHostOpts(),
            sopts=self.getSwitchOpts()),
            switch=HalonSwitch,
            host=HalonHost,
            link=HalonLink, controller=None,
            build=True)

    def user_config(self):
        s1 = self.net.switches[ 0 ]
        h1 = self.net.hosts[ 0 ]

        # HALON_TODO: Remove the sleep. The Docker start scripts
        # Should wait until the HalonSwitch is up. Tests shouldn't
        # put a sleep like this.
        time.sleep(3)

        info("Expecting interface '1' to be present in the DB.\n")
        out = s1.cmd("/usr/bin/ovs-vsctl list interface 1")
        if '_uuid' in out:
            info("Found Interface '1' in the OVSDB\n")
        else:
            error("Failed to find Interface '1' in the OVSBD.\n")

        out = s1.cmd("/usr/bin/ovs-vsctl set interface 1 user_config:admin=up")
        debug(out)
        time.sleep(2)

        # Make sure that 'error' status is 'module_missing'
        out = s1.cmd("/usr/bin/ovs-vsctl list interface 1")
        if 'module_missing' in out:
            info("Interface status is correctly set to 'module_missing'\n")
        else:
            info(out)
            assert 0, "Expected the interface status to be 'module_missing'"


    # Set pm_info to valid values, and verify that 'intfd'
    # changes the "error" state to something other than 'module_missing'
    def pm_info(self):
        s1 = self.net.switches[ 0 ]
        h1 = self.net.hosts[ 0 ]

        s1.cmd("/usr/bin/ovs-vsctl set interface 1 user_config:admin=up")

        # Set the pluggable module info to valid values.
        out = s1.cmd("/usr/bin/ovs-vsctl set interface 1 " +
                     "pm_info:connector=SFP_RJ45 pm_info:connector_status=supported")
        debug(out)
        time.sleep(1)

        # Make sure that 'error' status is not 'module_missing'
        out = s1.cmd("/usr/bin/ovs-vsctl list interface 1")
        if 'module_missing' in out:
            info(out)
            assert 0, "Expected the interface status not to be 'module_missing'"
        else:
            info("Interface pluggable module status is correctly recognised.\n")

class Test_intfd:

    # Create the Mininet topology based on mininet.
    test = intfdTest()

    def setup(self):
        pass

    def teardown(self):
        pass

    def setup_class(cls):
        pass

    def teardown_class(cls):
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
        self.test.pm_info()
