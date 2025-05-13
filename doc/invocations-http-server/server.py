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

import jinja2
import json
import re
import os
import subprocess
import werkzeug.exceptions
import werkzeug.routing
import werkzeug.utils

from werkzeug.wrappers import Request, Response
from werkzeug.middleware.shared_data import SharedDataMiddleware

def core_config(conf):
    new_conf = {}
    for k, v in conf.items():
        if v is not None:
            new_conf[k] = v
    return new_conf

class HexIdentifierConverter(werkzeug.routing.BaseConverter):
    regex = '[a-zA-Z0-9]{2,300}'

class HashIdentifierConverter(werkzeug.routing.BaseConverter):
    regex = '[a-zA-Z0-9]{40,64}'

class InvocationIdentifierConverter(werkzeug.routing.BaseConverter):
    regex = '[-:_a-zA-Z0-9]{1,200}'

class FileIdentifierConverter(werkzeug.routing.BaseConverter):
    regex = '[-:_.a-zA-Z0-9]{3,200}'

class InvocationServer:
    def __init__(self, logsdir, *,
                 just_mr = None,
                 graph = "graph.json",
                 artifacts = "artifacts.json",
                 profile = "profile.json",
                 meta = "meta.json"):
        self.logsdir = logsdir
        if just_mr is None:
            self.just_mr = ["just-mr"]
        else:
            self.just_mr = just_mr
        self.profile = profile
        self.graph = graph
        self.artifacts = artifacts
        self.meta = meta
        self.templatepath = os.path.join(os.path.dirname(__file__), "templates")
        self.jinjaenv = jinja2.Environment(
                loader=jinja2.FileSystemLoader(self.templatepath))
        rule = werkzeug.routing.Rule
        self.routingmap = werkzeug.routing.Map([
            rule("/", methods=("GET",), endpoint="list_invocations"),
            rule("/filterinvocations/context/<hexidentifier:key>/<hexidentifier:value>",
                 methods=("GET",), endpoint="filter_context"),
            rule("/filterinvocations/noncached",
                 methods=("GET",), endpoint="filter_noncached"),
            rule("/filterinvocations/remote/prop/<hexidentifier:key>/<hexidentifier:value>",
                 methods=("GET",), endpoint="filter_remote_prop"),
            rule("/blob/<hashidentifier:blob>",
                 methods=("GET",),
                 endpoint="get_blob"),
            rule("/blob/<hashidentifier:blob>/<fileidentifier:name>",
                 methods=("GET",),
                 endpoint="get_blob_as"),
            rule("/tree/<hashidentifier:tree>",
                 methods=("GET",),
                 endpoint="get_tree"),
            rule("/invocations/<invocationidentifier:invocation>",
                 methods=("GET",),
                 endpoint="get_invocation"),
        ], converters=dict(
            invocationidentifier=InvocationIdentifierConverter,
            hashidentifier=HashIdentifierConverter,
            hexidentifier=HexIdentifierConverter,
            fileidentifier=FileIdentifierConverter,
        ))

    @Request.application
    def __call__(self, request):
        mapadapter = self.routingmap.bind_to_environ(request.environ)
        try:
            endpoint, args = mapadapter.match()
            return getattr(self, "do_%s" % (endpoint,))(**args)
        except werkzeug.exceptions.HTTPException as e:
            return e

    def render(self, templatename, params):
        response = Response()
        response.content_type = "text/html; charset=utf8"
        template = self.jinjaenv.get_template(templatename)
        response.data = template.render(params).encode("utf8")
        return response

    def do_list_invocations(self, *,
                            filter_info="",
                            profile_filter = lambda p : True,
                            metadata_filter = lambda p : True):
        invocations = []
        count = 0
        entries = sorted(os.listdir(self.logsdir), reverse=True)
        context_filters = {}
        remote_props_filters = {}
        def add_filter(data, filters):
            for k, v in data.items():
                key = json.dumps(k)
                value = json.dumps(v)
                if key not in filters:
                    filters[key] = set()
                filters[key].add(value)
        for e in entries:
            profile = os.path.join(self.logsdir, e, self.profile)
            if not os.path.exists(profile):
                # only consider invocations that provide a profile; unfinished
                # invocations as well as not build related ones (like
                # install-cas) are not relevant.
                continue
            try:
                with open(profile) as f:
                    profile_data = json.load(f)
            except:
                profile_data = {}

            meta = os.path.join(self.logsdir, e, self.meta)
            try:
                with open(meta) as f:
                    meta_data = json.load(f)
            except:
                meta_data = {}

            if (not profile_filter(profile_data)) or (not metadata_filter(meta_data)):
                continue
            count += 1
            target = profile_data.get("target")
            config = core_config(profile_data.get("configuration", {}))
            context = meta_data.get("context", {})
            remote_props = profile_data.get('remote', {}).get('properties', {})
            add_filter(context, context_filters)
            add_filter(remote_props, remote_props_filters)
            invocation = {
                "name": e,
                "subcommand": profile_data.get("subcommand"),
                "target": json.dumps(target) if target else None,
                "config": json.dumps(config) if config else None,
                "context": json.dumps(context) if context else None,
                "exit_code": profile_data.get('exit code', 0),
                "remote_address": profile_data.get('remote', {}).get('address'),
                "remote_props": json.dumps(remote_props) if remote_props else None,
            }
            invocations.append(invocation)
            if count >= 100:
                break
        def convert_filters(filters):
            return [{
                "key": key,
                "key_hex": key.encode().hex(),
                "values": [{
                    "value": v,
                    "value_hex": v.encode().hex(),
                } for v in values]
            } for key, values in filters.items() if len(values) > 1]
        return self.render("invocations.html",
                           {"invocations": invocations,
                            "filter_info": filter_info,
                            "context_filters": convert_filters(context_filters),
                            "remote_props_filters": convert_filters(remote_props_filters)})

    def do_filter_remote_prop(self, key, value):
        filter_info = "remote-execution property"
        try:
            key_string = json.loads(bytes.fromhex(key).decode('utf-8'))
            value_string = json.loads(bytes.fromhex(value).decode('utf-8'))
            filter_info += " " + json.dumps({key_string: value_string})
        except:
            pass

        def check_prop(p):
            for k, v in p.get('remote', {}).get('properties', {}).items():
                if (json.dumps(k).encode().hex() == key) and (json.dumps(v).encode().hex() == value):
                    return True
            return False

        return self.do_list_invocations(
            filter_info = filter_info,
            profile_filter = check_prop,
        )

    def do_filter_context(self, key, value):
        filter_info = "context variable"
        try:
            key_string = json.loads(bytes.fromhex(key).decode('utf-8'))
            value_string = json.loads(bytes.fromhex(value).decode('utf-8'))
            filter_info += " " + json.dumps({key_string: value_string})
        except:
            pass

        def check_prop(p):
            for k, v in p.get('context', {}).items():
                if (json.dumps(k).encode().hex() == key) and (json.dumps(v).encode().hex() == value):
                    return True
            return False

        return self.do_list_invocations(
            filter_info = filter_info,
            metadata_filter = check_prop,
        )

    def do_filter_noncached(self):
        def check_noncached(p):
            for v in p.get('actions', {}).values():
                if not v.get('cached'):
                    return True
            return False
        return self.do_list_invocations(
            filter_info = "not fully cached",
            profile_filter = check_noncached,
        )

    def process_failure(self, cmd, procobj, *, failure_kind=None):
        params = {"stdout": None, "stderr": None, "failure_kind": failure_kind}
        params["cmd"] = json.dumps(cmd)
        params["exit_code"] = procobj.returncode
        try:
            params["stdout"] = procobj.stdout.decode('utf-8')
        except:
            pass
        try:
            params["stderr"] = procobj.stderr.decode('utf-8')
        except:
            pass
        return self.render("failure.html", params)

    def do_get_blob(self, blob, *, download_as=None):
        cmd = self.just_mr + ["install-cas", "--remember", blob]
        blob_data = subprocess.run(cmd,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        if blob_data.returncode !=0:
            return self.process_failure(cmd, blob_data)
        try:
            blob_content = blob_data.stdout.decode('utf-8')
        except:
            # Not utf-8, so return as binary file to download separately
            download_as = download_as or blob
        if download_as:
            response = Response()
            response.content_type = "application/octet-stream"
            response.data = blob_data.stdout
            response.headers['Content-Disposition'] = \
                "attachement; filename=%s" %(download_as,)
            return response
        return self.render("blob.html",
                           {"name": blob,
                            "data": blob_content})

    def do_get_blob_as(self, blob, name):
        return self.do_get_blob(blob, download_as=name)

    def do_get_tree(self, tree):
        cmd = self.just_mr + ["install-cas", "%s::t" % (tree,)]
        tree_data = subprocess.run(cmd,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        if tree_data.returncode !=0:
            return self.process_failure(cmd, tree_data)
        try:
            tree_info = json.loads(tree_data.stdout.decode('utf-8'))
        except Exception as e:
            return self.process_failure(
                cmd, tree_data,
                failure_kind = "Malformed output: %s" % (e,))

        entries = []
        for k, v in tree_info.items():
            h, s, t = v.strip('[]').split(':')
            entries.append({"path": k,
                            "hash": h,
                            "type": "tree" if t == 't' else "blob"})
        return self.render("tree.html", {"name": tree,
                                         "entries": entries})

    def do_get_invocation(self, invocation):
        params = {"invocation": invocation,
                  "have_profile": False}

        try:
            with open(os.path.join(
                    self.logsdir, invocation, self.profile)) as f:
                profile = json.load(f)
                params["have_profile"] = True
        except:
            profile = {}

        try:
            with open(os.path.join(
                    self.logsdir, invocation, self.meta)) as f:
                meta = json.load(f)
        except:
            meta = {}

        try:
            with open(os.path.join(
                    self.logsdir, invocation, self.graph)) as f:
                graph = json.load(f)
        except:
            graph = {}

        try:
            with open(os.path.join(
                    self.logsdir, invocation, self.artifacts)) as f:
                artifacts = json.load(f)
        except:
            artifacts = {}

        params["repo_config"] = meta.get('configuration')
        params["exit_code"] = profile.get('exit code')
        analysis_errors = []
        blob_pattern = re.compile(r'blob ([0-9A-Za-z]{40,64})')
        for s in profile.get('analysis errors', []):
            analysis_errors.append({
                "msg": s,
                "blobs": blob_pattern.findall(s),
            })
        params["analysis_errors"] = analysis_errors
        params["remote_address"] = profile.get('remote', {}).get('address')
        remote_props = []
        for k, v in profile.get('remote', {}).get('properties', {}).items():
            key = json.dumps(k)
            value = json.dumps(v)
            remote_props.append({
                "key": key,
                "key_hex": key.encode().hex(),
                "value": value,
                "value_hex": value.encode().hex(),
            })
        params["remote_props"] = remote_props
        params["remote_dispatch"] = json.dumps(
            profile.get('remote', {}).get('dispatch', []))
        # For complex conditional data fill with None as default
        for k in ["cmdline", "cmd", "target", "config"]:
            params[k] = None
        # Fill this data, if available
        if meta.get('cmdline'):
            params["cmdline"] = json.dumps(meta.get('cmdline'))
        context = []
        for k, v in meta.get('context', {}).items():
            key = json.dumps(k)
            value = json.dumps(v)
            context.append({
                "key": key,
                "key_hex": key.encode().hex(),
                "value": value,
                "value_hex": value.encode().hex(),
            })
        params["context"] = context
        if profile.get('subcommand'):
            params["cmd"] = json.dumps(
                [profile.get('subcommand')] + profile.get('subcommand args', []))
        if profile.get('target'):
            params["target"] = json.dumps(profile.get('target'))
        if profile.get('configuration') is not None:
            params["config"] = json.dumps(core_config(
                profile.get('configuration')))

        output_artifacts = []
        for k, v in artifacts.items():
            output_artifacts.append({
                "path": k,
                "basename": os.path.basename(k),
                "hash": v["id"],
                "type": "tree" if v["file_type"] == "t" else "blob",
            })
        params["artifacts"] = output_artifacts
        params["artifacts_count"] = len(output_artifacts)

        def action_data(name, profile_value):
            data = { "name_prefix": "",
                     "name": name,
                     "cached": profile_value.get('cached'),
                     "exit_code": profile_value.get('exit code', 0)}
            duration = profile_value.get('duration', 0.0)
            data["duration"] = '%0.3fs' % (duration,) if duration > 0 else None
            desc = graph.get('actions', {}).get(name, {})
            data["may_fail"] = desc.get("may_fail")
            data["stdout"] = profile_value.get('stdout')
            data["stderr"] = profile_value.get('stderr')
            data["output"] = desc.get('output', [])
            data["output_dirs"] = desc.get('output_dirs', [])
            if len(data["output"]) == 1:
                data["primary_output"] = data["output"][0]
            elif len(data["output"]) == 0 and len(data["output_dirs"]) == 1:
                data["primary_output"] = data["output_dirs"][0]
            else:
                data["primary_output"] = None
            data["artifacts"] = profile_value.get('artifacts', {})
            origins = []
            for origin in desc.get('origins', []):
                origins.append("%s#%d@%s" % (
                    json.dumps(origin.get('target', [])),
                    origin.get('subtask', 0),
                    core_config(origin.get('config', {})),))
            data["origins"] = origins
            return data

        actions_considered = set()
        failed_build_actions = []
        failed_test_actions = []
        for k, v in profile.get('actions', {}).items():
            if v.get('exit code', 0) != 0:
                actions_considered.add(k)
                if graph.get('actions', {}).get(k, {}).get('may_fail') != None:
                    failed_test_actions.append(action_data(k, v))
                else:
                    failed_build_actions.append(action_data(k, v))
        params["failed_actions"] = failed_build_actions + failed_test_actions

        # non-failed actions with output
        output_actions = []
        for k, v in profile.get('actions', {}).items():
            if k not in actions_considered:
               if v.get('stdout') or v.get('stderr'):
                   actions_considered.add(k)
                   output_actions.append(action_data(k,v))
        params["output_actions"] = output_actions

        # longest running non-cached non-failed actions without output
        candidates = []
        action_count = 0
        action_count_cached = 0
        for k, v in profile.get('actions', {}).items():
            action_count += 1
            if not v.get('cached'):
                if k not in actions_considered:
                    candidates.append((v.get('duration', 0.0), k, v))
            else:
                action_count_cached += 1
        params["action_count"] = action_count
        params["action_count_cached"] = action_count_cached
        candidates.sort(reverse=True)
        non_cached = []
        params["more_noncached"] = None
        if len(candidates) > 30:
            params["more_non_cached"] = len(candidates) - 30
            candidates = candidates[:30]
        for t, k, v in candidates:
            action = action_data(k, v)
            action["name_prefix"] = "%5.1fs" % (t,)
            non_cached.append(action)
        params["non_cached"] = non_cached

        return self.render("invocation.html", params)

def create_app(logsdir, **kwargs):
    app = InvocationServer(logsdir, **kwargs)
    app = SharedDataMiddleware(app, {
      '/static': os.path.join(os.path.dirname(__file__), 'static')
    })
    return app

if __name__ == '__main__':
    import sys
    from argparse import ArgumentParser
    from wsgiref.simple_server import make_server
    parser = ArgumentParser(
        description="Serve invocations of a given directory")
    parser.add_argument("--meta", dest="meta", default="meta.json",
                        help="Name of the logged metadata file")
    parser.add_argument("--graph", dest="graph", default="graph.json",
                        help="Name of the logged action-graph file")
    parser.add_argument("--artifacts", dest="artifacts", default="artifacts.json",
                        help="Name of the logged artifacts file")
    parser.add_argument("--profile", dest="profile", default="profile.json",
                        help="Name of the logged profile file")
    parser.add_argument(
        "--just-mr", dest="just_mr", default='["just-mr"]',
        help="The correct way of invoking just-mr, as JSON vector of strings")
    parser.add_argument("-i", dest="interface", default='127.0.0.1',
                        help="The interface to listen on")
    parser.add_argument("-p", dest="port", default=9000, type=int,
                        help="The port to listen on")
    parser.add_argument("DIR",
                        help="Directory (for a single project id) to serve")
    args = parser.parse_args()
    try:
        just_mr = json.loads(args.just_mr)
    except Exception as e:
        print("just-mr argument should be valid json, but %r is not: %s"
              % (args.just_mr, e), file=sys.stderr)
        sys.exit(1)
    app = create_app(args.DIR,
                     just_mr=just_mr,
                     graph=args.graph,
                     artifacts=args.artifacts,
                     meta=args.meta,
                     profile=args.profile)
    make_server(args.interface, args.port, app).serve_forever()
