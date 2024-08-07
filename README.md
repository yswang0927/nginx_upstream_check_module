# 更新记录

- 2024/07/24 因tengine的check_module存在问题：配置 `location /check_stauts {check_status;}` 指令不显示数据，先恢复到yaoweibin的代码，并merge pull/257的代码。
- 2024/07/20 更新为 https://github.com/alibaba/tengine/tree/master/modules/ngx_http_upstream_check_module 中的最新代码。


Name
    nginx_http_upstream_check_module - support upstream health check with
    Nginx

Synopsis
    http {

        upstream cluster {

            # simple round-robin
            server 192.168.0.1:80;
            server 192.168.0.2:80;

            check interval=5000 rise=1 fall=3 timeout=4000;

            #check interval=3000 rise=2 fall=5 timeout=1000 type=ssl_hello;

            #check interval=3000 rise=2 fall=5 timeout=1000 type=http;
            #check_http_send "HEAD / HTTP/1.0\r\n\r\n";
            #check_http_expect_alive http_2xx http_3xx;
        }

        server {
            listen 80;

            location / {
                proxy_pass http://cluster;
            }

            location /status {
                check_status;

                access_log   off;
                allow SOME.IP.ADD.RESS;
                deny all;
           }
        }

    }

Description
    Add the support of health check with the upstream servers.

Directives
  check
    syntax: *check interval=milliseconds [fall=count] [rise=count]
    [timeout=milliseconds] [default_down=true|false]
    [type=tcp|http|ssl_hello|mysql|ajp|fastcgi]*

    default: *none, if parameters omitted, default parameters are
    interval=30000 fall=5 rise=2 timeout=1000 default_down=true type=tcp*

    context: *upstream*

    description: Add the health check for the upstream servers.

    The parameters' meanings are:

    *   *interval*: the check request's interval time.

    *   *fall*(fall_count): After fall_count check failures, the server is
        marked down.

    *   *rise*(rise_count): After rise_count check success, the server is
        marked up.

    *   *timeout*: the check request's timeout.

    *   *default_down*: set initial state of backend server, default is
        down.

    *   *port*: specify the check port in the backend servers. It can be
        different with the original servers port. Default the port is 0 and
        it means the same as the original backend server.

    *   *type*: the check protocol type:

        1.  *tcp* is a simple tcp socket connect and peek one byte.

        2.  *ssl_hello* sends a client ssl hello packet and receives the
            server ssl hello packet.

        3.  *http* sends a http request packet, receives and parses the http
            response to diagnose if the upstream server is alive.

        4.  *mysql* connects to the mysql server, receives the greeting
            response to diagnose if the upstream server is alive.

        5.  *ajp* sends a AJP Cping packet, receives and parses the AJP
            Cpong response to diagnose if the upstream server is alive.

        6.  *fastcgi* send a fastcgi request, receives and parses the
            fastcgi response to diagnose if the upstream server is alive.

  check_http_send
    syntax: *check_http_send http_packet*

    default: *"GET / HTTP/1.0\r\n\r\n"*

    context: *upstream*

    description: If you set the check type is http, then the check function
    will sends this http packet to check the upstream server.

  check_http_expect_alive
    syntax: *check_http_expect_alive [ http_2xx | http_3xx | http_4xx |
    http_5xx ]*

    default: *http_2xx | http_3xx*

    context: *upstream*

    description: These status codes indicate the upstream server's http
    response is ok, the backend is alive.

  check_keepalive_requests
    syntax: *check_keepalive_requests num*

    default: *check_keepalive_requests 1*

    context: *upstream*

    description: The directive specifies the number of requests sent on a
    connection, the default vaule 1 indicates that nginx will certainly
    close the connection after a request.

  check_fastcgi_param
    Syntax: *check_fastcgi_params parameter value*

    default: see below

    context: *upstream*

    description: If you set the check type is fastcgi, then the check
    function will sends this fastcgi headers to check the upstream server.
    The default directive looks like:

          check_fastcgi_param "REQUEST_METHOD" "GET";
          check_fastcgi_param "REQUEST_URI" "/";
          check_fastcgi_param "SCRIPT_FILENAME" "index.php";

  check_shm_size
    syntax: *check_shm_size size*

    default: *1M*

    context: *http*

    description: Default size is one megabytes. If you check thousands of
    servers, the shared memory for health check may be not enough, you can
    enlarge it with this directive.

  check_status
    syntax: *check_status [html|csv|json]*

    default: *none*

    context: *location*

    description: Display the health checking servers' status by HTTP. This
    directive should be set in the http block.

    You can specify the default display format. The formats can be `html`,
    `csv` or `json`. The default type is `html`. It also supports to specify
    the format by the request argument. Suppose your `check_status` location
    is '/status', the argument of `format` can change the display page's
    format. You can do like this:

        /status?format=html
        /status?format=csv
        /status?format=json

    At present, you can fetch the list of servers with the same status by
    the argument of `status`. For example:

        /status?format=html&status=down
        /status?format=csv&status=up

    Below it's the sample html page:

        <!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN
        "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
        <title>Nginx http upstream check status</title>
            <h1>Nginx http upstream check status</h1>
            <h2>Check upstream server number: 1, generation: 3</h2>
                    <th>Index</th>
                    <th>Upstream</th>
                    <th>Name</th>
                    <th>Status</th>
                    <th>Rise counts</th>
                    <th>Fall counts</th>
                    <th>Check type</th>
                    <th>Check port</th>
                    <td>0</td>
                    <td>backend</td>
                    <td>106.187.48.116:80</td>
                    <td>up</td>
                    <td>39</td>
                    <td>0</td>
                    <td>http</td>
                    <td>80</td>

    Below it's the sample of csv page:

        0,backend,106.187.48.116:80,up,46,0,http,80

    Below it's the sample of json page:

        {"servers": {
          "total": 1,
          "generation": 3,
          "server": [
           {"index": 0, "upstream": "backend", "name": "106.187.48.116:80", "status": "up", "rise": 58, "fall": 0, "type": "http", "port": 80}
          ]
         }}

Installation
    Download the latest version of the release tarball of this module from
    github (<http://github.com/yaoweibin/nginx_upstream_check_module>)

    Grab the nginx source code from nginx.org (<http://nginx.org/>), for
    example, the version 1.0.14 (see nginx compatibility), and then build
    the source with this module:

        $ wget 'http://nginx.org/download/nginx-1.0.14.tar.gz'
        $ tar -xzvf nginx-1.0.14.tar.gz
        $ cd nginx-1.0.14/
        $ patch -p1 < /path/to/nginx_http_upstream_check_module/check.patch

        $ ./configure --add-module=/path/to/nginx_http_upstream_check_module

        $ make
        $ make install

Note
    If you use nginx-1.2.1 or nginx-1.3.0, the nginx upstream round robin
    module changed greatly. You should use the patch named
    'check_1.2.1.patch'.

    If you use nginx-1.2.2+ or nginx-1.3.1+, It added the upstream
    least_conn module. You should use the patch named 'check_1.2.2+.patch'.

    If you use nginx-1.2.6+ or nginx-1.3.9+, It adjusted the round robin
    module. You should use the patch named 'check_1.2.6+.patch'.

    If you use nginx-1.5.12+, You should use the patch named
    'check_1.5.12+.patch'.

    If you use nginx-1.7.2+, You should use the patch named
    'check_1.7.2+.patch'.

    The patch just adds the support for the official Round-Robin, Ip_hash
    and least_conn upstream module. But it's easy to expand my module to
    other upstream modules. See the patch for detail.

    If you want to add the support for upstream fair module, you can do it
    like this:

        $ git clone git://github.com/gnosek/nginx-upstream-fair.git
        $ cd nginx-upstream-fair
        $ patch -p2 < /path/to/nginx_http_upstream_check_module/upstream_fair.patch
        $ cd /path/to/nginx-1.0.14
        $ ./configure --add-module=/path/to/nginx_http_upstream_check_module --add-module=/path/to/nginx-upstream-fair-module
        $ make
        $ make install

    If you want to add the support for nginx sticky module, you can do it
    like this:

        $ svn checkout http://nginx-sticky-module.googlecode.com/svn/trunk/ nginx-sticky-module
        $ cd nginx-sticky-module
        $ patch -p0 < /path/to/nginx_http_upstream_check_module/nginx-sticky-module.patch
        $ cd /path/to/nginx-1.0.14
        $ ./configure --add-module=/path/to/nginx_http_upstream_check_module --add-module=/path/to/nginx-sticky-module
        $ make
        $ make install

    Note that, the nginx-sticky-module also needs the original check.patch.

Compatibility
    *   The module version 0.1.5 should be compatibility with 0.7.67+

    *   The module version 0.1.8 should be compatibility with Nginx-1.0.14+

Notes
TODO
Known Issues
Changelogs
  v0.3
    *   support keepalive check requests

    *   fastcgi check requests

    *   json/csv check status page support

  v0.1
    *   first release

Authors
    Weibin Yao(姚伟斌) *yaoweibin at gmail dot com*

    Matthieu Tourne

Copyright & License
    This README template copy from agentzh (<http://github.com/agentzh>).

    The health check part is borrowed the design of Jack Lindamood's
    healthcheck module healthcheck_nginx_upstreams
    (<http://github.com/cep21/healthcheck_nginx_upstreams>);

    This module is licensed under the BSD license.

    Copyright (C) 2014 by Weibin Yao <yaoweibin@gmail.com>

    Copyright (C) 2010-2014 Alibaba Group Holding Limited

    Copyright (C) 2014 by LiangBin Li

    Copyright (C) 2014 by Zhuo Yuan

    Copyright (C) 2012 by Matthieu Tourne

    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:

    *   Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.

    *   Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
    IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
    TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
    PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
    TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
    PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
    LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
    NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

