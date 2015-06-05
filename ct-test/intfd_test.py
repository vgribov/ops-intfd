#!/usr/bin/python

import os
import sys
import time

# HALON_TODO: Use the python environment built by Yocto
#             for the host environment, rather than using
#             the default host python. Then we can remove
#             the hard-coded python paths.
if 'BUILD_ROOT' in os.environ:
    BUILD_ROOT = os.environ['BUILD_ROOT']
else:
    BUILD_ROOT = "../../.."
HALON_VSI_LIB = BUILD_ROOT + "/src/halon-vsi"
print HALON_VSI_LIB
sys.path.append(HALON_VSI_LIB)

import mininet
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

    def test(self):
        s1 = self.net.switches[ 0 ]
        h1 = self.net.hosts[ 0 ]

        info("Expecting interface '1' to be present in the DB.\n")
        out = s1.cmd("/usr/bin/ovs-vsctl list interface 1")
        if '_uuid' in out:
            info("Found Interface '1' in the OVSDB\n")
        else:
            error("Failed to find Interface '1' in the OVSBD.\n")

        out = s1.cmd("/usr/bin/ovs-vsctl set interface 1 user_config:admin=up")
        debug(out)
        time.sleep(0.3)

        # Make sure that 'error' status is 'module_missing'
        out = s1.cmd("/usr/bin/ovs-vsctl list interface 1")
        if 'module_missing' in out:
            info("Interface status is correctly set to 'module_missing'\n")
        else:
            error("Expected the interface status to be 'module_missing'\n")
            info(out)
            return False

        # Set the pluggable module info to valid values.
        out = s1.cmd("/usr/bin/ovs-vsctl set interface 1 " +
                     "pm_info:connector=SFP_RJ45 pm_info:connector_status=supported")
        debug(out)
        time.sleep(0.3)

        # Make sure that 'error' status is not 'module_missing'
        out = s1.cmd("/usr/bin/ovs-vsctl list interface 1")
        if 'module_missing' not in out:
            info("Interface pluggable module status is correctly recognised.\n")
        else:
            error("Expected the interface status not to be 'module_missing'\n")
            info(out)
            return False

        return True

if __name__ == '__main__':
    test = intfdTest()
    test.run(runCLI=False)
