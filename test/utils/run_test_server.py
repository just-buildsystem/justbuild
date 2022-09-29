#!/usr/bin/env python3
# Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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
import signal
from http.server import SimpleHTTPRequestHandler as HTTPHandler
from http.server import HTTPServer as HTTPServer

httpd = None


# handle interrupts gracefully, i.e., shutdown the server and exit
def RecvSig(*_) -> None:
    if not httpd is None:
        # cleanup
        httpd.server_close()
    sys.exit(0)


if __name__ == "__main__":
    # set calback for usual terminating signals
    signal.signal(signal.SIGHUP, RecvSig)
    signal.signal(signal.SIGINT, RecvSig)
    signal.signal(signal.SIGTERM, RecvSig)

    # set up server args
    hostname = "127.0.0.1"
    # setup server obj
    port = 0
    with HTTPServer((hostname, 0), HTTPHandler) as httpd:
        # print port number
        socket_info = httpd.socket.getsockname()
        print(socket_info[1])
        # run server
        httpd.serve_forever()
