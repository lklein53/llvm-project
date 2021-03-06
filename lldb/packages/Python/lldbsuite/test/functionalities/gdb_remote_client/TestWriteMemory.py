from __future__ import print_function
import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test.decorators import *
from gdbclientutils import *


class TestWriteMemory(GDBRemoteTestBase):

    def test(self):

        class MyResponder(MockGDBServerResponder):
            def setBreakpoint(self, packet):
                return "OK"

    def setUp(self):
        super(TestWriteMemory, self).setUp()
        self._initial_platform = lldb.DBG.GetSelectedPlatform()

    def tearDown(self):
        lldb.DBG.SetSelectedPlatform(self._initial_platform)
        super(TestWriteMemory, self).tearDown()

        self.server.responder = MyResponder()
        target = self.dbg.CreateTargetWithFileAndTargetTriple('', 'x86_64-pc-linux')
        process = self.connect(target)

        bp = target.BreakpointCreateByAddress(0x1000)
        self.assertTrue(bp.IsValid())
        self.assertEqual(bp.GetNumLocations(), 1)
        bp.SetEnabled(True)
        self.assertTrue(bp.IsEnabled())

        err = lldb.SBError()
        data = str("\x01\x02\x03\x04")
        result = process.WriteMemory(0x1000, data, err)
        self.assertEqual(result, 4)
