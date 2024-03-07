#!/usr/bin/env python3
# Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import sys
import socketserver


log_file = "access.log"

class MyTCPHandler(socketserver.BaseRequestHandler):
    def handle(self):
        with open(log_file, "a") as f:
            f.write("Connected from {}\n".format(self.client_address,))
        self.request.sendall(b"Wrong, but thanks for playing")

log_file = sys.argv[2]

with socketserver.TCPServer(("127.0.0.1", 0), MyTCPHandler) as server:
    socket_info = server.socket.getsockname()
    with open(sys.argv[1], "w") as f:
        f.write("%d" % (socket_info[1],))
    server.serve_forever()
