#!/usr/bin/env python3
# Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

import json

from typing import Any, Dict, cast

SIZE=1000

deep : Dict[str, Any] = (
    {"deep%d (unexported)" % (i,) :
     {"type": "generic",
      "deps": ["deep%d" % (i-1,)] if i > 0 else [],
      "outs": ["out%d" % (i,)],
      "cmds": ["cat out%d > out%d" % (i-1, i)
               if i > 0 else "true",
               "echo %d >> out%d" % (i, i)]}
     for i in range(SIZE)}
    | {"deep%d" % (i,) :
       {"type": "export", "target": "deep%d (unexported)" %(i,)}
       for i in range(SIZE)}
)

wide : Dict[str, Any] = (
    cast(Dict[str, Any],
         {"wide":
          {"type": "generic",
           "outs": ["out"],
           "cmds": ["for f in $(ls wide* | sort) ; do cat $f >> out ; done"],
           "deps": ["wide%d" % (i,) for i in range(SIZE)]
           }})
     | cast(Dict[str, Any],
            {"wide%d (unexported)" % (i,) :
             {"type": "generic",
              "outs": ["wide%d" % (i,)],
              "cmds": ["echo %d > wide%d" % (i,i)]}
             for i in range(SIZE)})
     | cast(Dict[str, Any],
            {"wide%d" % (i,) :
             {"type": "export", "target": "wide%d (unexported)" %(i,)}
             for i in range(SIZE)})
)

total : Dict[str, Any] = (
    { "": {"type": "export", "target": "all"},
      "all": {"type": "install",
              "files": {"deep": "deep%d" % (SIZE-1,),
                        "wide": "wide"}}}
    | deep | wide)

with open("TARGETS", "w") as f:
    json.dump(total, f)
