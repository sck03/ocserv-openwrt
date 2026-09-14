/* SPDX-License-Identifier: GPL-3.0-or-later */
(function () {
    'use strict';
    var options = JSON.parse(document.getElementById('ocserv-easy-options').textContent);
    var root = document.getElementById('ocserv-easy');
    var chinese = /^zh/i.test(options.language || '');
    var state, activeTab = 'users', busy = false, modal = null, messageBox, content, statusBox;
    function t(zh, en) { return chinese ? zh : en; }
    function array(value) { return Array.isArray(value) ? value : []; }
    function element(tag, attrs, children) {
        var node = document.createElement(tag);
        Object.keys(attrs || {}).forEach(function (key) {
            if (key === 'className') node.className = attrs[key];
            else if (key === 'text') node.textContent = attrs[key];
            else if (key.slice(0, 2) === 'on') node.addEventListener(key.slice(2).toLowerCase(), attrs[key]);
            else if (key === 'value' || key === 'checked' || key === 'disabled' || key === 'hidden' || key === 'required') node[key] = attrs[key];
            else node.setAttribute(key, attrs[key]);
        });
        (children || []).forEach(function (child) { node.appendChild(typeof child === 'string' ? document.createTextNode(child) : child); });
        return node;
    }
    function button(label, handler, kind) {
        return element('button', {type:'button', className:'cbi-button ' + (kind || 'cbi-button-neutral'), text:label, onClick:handler});
    }
    function writable() { return !!options.writable && !!state && state.supported; }
    function mutationButton(label, handler, kind) {
        var b=button(label,handler,kind); b.disabled=!writable(); return b;
    }
    var errors = {
        invalid_username:t('账号限 1–64 位英文、数字、点、下划线、短横线或 @，以英文或数字开头。','Use 1–64 letters, digits, dots, underscores, hyphens or @. Start with a letter or digit.'),
        duplicate_user:t('这个账号已存在，请使用其他名称。','This username already exists.'),
        invalid_group:t('用户组格式无效；一般保持 * 即可。','Invalid group. Normally leave this as *.'),
        password_required:t('新增账号需要填写密码。','A password is required for a new account.'),
        invalid_password:t('密码至少 8 个字符，不能包含换行，UTF-8 长度最多 128 字节。','Use a password of at least 8 characters, without control characters, and at most 128 UTF-8 bytes.'),
        hash_failed:t('无法生成或保留密码，请设置新密码并检查系统的 SHA-512 crypt 支持。','Cannot generate or preserve this password. Set a new password and check SHA-512 crypt support.'),
        invalid_existing_user:t('现有配置中有不支持的账号名，请先在原“用户设置”页修正。','An existing username is unsupported. Correct it in the original User Settings page.'),
        invalid_existing_password:t('现有账号的密码记录无效，请先修正。','An existing account has an invalid password record.'),
        stale_revision:t('配置已被其他页面修改。请关闭此对话框，刷新页面后重试。','Configuration changed in another page. Close this dialog, refresh and try again.'),
        pending_changes:t('其他页面有尚未提交的 ocserv 修改，请先应用或撤销。','Another editor has pending ocserv changes. Apply or discard them first.'),
        busy:t('另一个操作正在进行，请稍后重试。','Another operation is in progress. Try again shortly.'),
        upgrade_required:t('请先升级到 ocserv 1.5.0 或后续版本。','Upgrade to ocserv 1.5.0 or later first.'),
        plain_auth_required:t('账号管理需要使用 plain 用户名密码认证。','Account management requires plain username/password authentication.'),
        custom_auth_file:t('正在使用自定义认证文件；请先恢复标准 UCI 账号配置。','A custom authentication file is active. Restore standard UCI account management first.'),
        proxy_arp_managed:t('此配置启用了代理 ARP，请在原服务设置页管理网络。','Proxy ARP is enabled. Manage networking in the original settings page.'),
        custom_override:t('额外配置文件覆盖了这些设置，请先在原设置中移除重复项。','The extra configuration file overrides these settings. Resolve duplicate directives in the original configuration first.'),
        config_check_failed:t('ocserv 配置检查未通过，未应用更改。请检查证书、地址池及原有额外配置；首次部署需先生成服务器证书。','ocserv rejected the proposed configuration. No changes were applied. Check certificates, address pools and extra configuration; a new deployment needs server certificates first.'),
        rollback_failed:t('回滚未完成，请检查服务状态。上次配置保存在 /etc/ocserv/easy-backup。','Rollback was incomplete. Check the service. Previous settings are in /etc/ocserv/easy-backup.'),
        restart_failed:t('服务未能重新启动，已尝试恢复原配置。请检查服务日志。','The service did not restart. Restoration of the previous configuration was attempted. Check the service log.'),
        terminate_failed:t('未能确认该账号的所有登录已失效，账号修改已回滚。请检查 occtl 和服务状态后重试。','Could not confirm session revocation. The account change was rolled back. Check occtl and the service, then retry.'),
        disconnect_failed:t('该连接可能已经断开，请刷新在线列表。','The connection may already be closed. Refresh the online list.'),
        forbidden:t('当前登录只有查看权限。','Your login has read-only access.'),
        session_expired:t('管理登录可能已过期，请刷新页面重新登录。','Your administration login may have expired. Refresh and sign in again.'),
        invalid_pool:t('请填写有效 IPv4 网段，掩码范围为 /8 至 /30。','Enter a valid IPv4 network with a /8 to /30 mask.'),
        pool_not_network:t('地址池应填写网段地址，例如 10.77.0.0，不能填写 10.77.0.1。','Use a network address such as 10.77.0.0, not 10.77.0.1.'),
        invalid_dns:t('请填写 1–3 个有效 DNS 服务器 IP，每行一个。','Enter 1–3 valid DNS server IP addresses, one per line.'),
        duplicate_dns:t('DNS 地址不能重复。','DNS addresses must be unique.'),
        dns_is_endpoint:t('DNS 请使用服务端的隧道 IP，例如 10.77.0.1，不要使用 VPN 入口地址。','Use the server tunnel IP for DNS, such as 10.77.0.1, rather than the VPN endpoint address.'),
        invalid_routes:t('请填写有效网段，每行一个，例如 192.168.19.0/24。','Enter valid network routes, one per line, such as 192.168.19.0/24.'),
        route_needs_ipv6:t('下发 IPv6 路由前，需要先设置 IPv6 地址池。','Configure an IPv6 address pool before advertising IPv6 routes.'),
        invalid_ipv6_pool:t('IPv6 地址池应使用地址/前缀格式，前缀范围为 16–112。','Use an IPv6 address/prefix with a prefix length of 16–112.'),
        invalid_url:t('请输入完整的 HTTPS 地址，例如 https://vpn.example.com:4443。','Enter a complete HTTPS address, such as https://vpn.example.com:4443.'),
        invalid_domain:t('默认域名格式无效。','The default domain is invalid.'),
        invalid_port:t('端口范围为 1–65535。','Ports must be between 1 and 65535.'),
        invalid_client_limit:t('同账号在线数不能大于总在线数。','The per-account limit cannot exceed the total connection limit.'),
        write_failed:t('保存失败，请检查剩余空间及文件权限。','Saving failed. Check free space and file permissions.'),
        service_failed:t('服务操作失败，请检查 ocserv 日志。','Service operation failed. Check the ocserv log.'),
        unknown_schema:t('未找到标准 ocserv UCI 配置，请先安装并初始化 ocserv。','Standard ocserv UCI configuration was not found. Install and initialize ocserv first.'),
        ca_unavailable:t('服务器 CA 证书不可用，请先完成证书配置。','The server CA certificate is unavailable. Complete certificate setup first.')
    };
    function errorText(error) { return errors[error.code] || t('操作失败，请检查输入或刷新后重试。','Operation failed. Check the input or refresh and retry.') + ' (' + String(error.code || 'network_error') + ')'; }
    function notice(text, isError) { messageBox.textContent=text; messageBox.className='easy-notice'+(isError?' error':''); messageBox.hidden=!text; }
    async function request(path, payload) {
        var settings={credentials:'same-origin',cache:'no-store'};
        if (payload) {
            settings.method='POST'; settings.headers={'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'};
            settings.body='token='+encodeURIComponent(options.token)+'&payload='+encodeURIComponent(JSON.stringify(payload));
        }
        var response=await fetch(options.base+'/'+path,settings);
        var data;
        try { data=await response.json(); } catch (_) { throw {code:'session_expired'}; }
        if (!response.ok || data.ok!==true) throw {code:data.error || 'network_error'};
        return data.data;
    }
    async function mutate(payload) {
        if (busy) throw {code:'busy'};
        busy=true;
        var buttons=Array.from(root.querySelectorAll('button')).map(function(b){var old=b.disabled;b.disabled=true;return [b,old];});
        try { payload.revision=state.revision; return await request('action',payload); }
        finally { busy=false; buttons.forEach(function(pair){pair[0].disabled=pair[1];}); }
    }
    var effects={saved:t('已保存，下次启动服务时生效。','Saved for the next service start.'),updated_live:t('账号已生效，其他在线用户保持连接。','Account updated. Other users remain connected.'),terminated:t('账号已更新，该账号的旧登录已失效。','Account updated and its previous sessions revoked.'),restarted:t('配置已应用，VPN 服务已重新启动。','Configuration applied and VPN service restarted.'),disconnected:t('已断开该连接。账号仍可重新登录。','Connection disconnected. The account can sign in again.'),service_changed:t('服务操作已执行，状态将自动刷新。','Service operation submitted. Status will refresh automatically.')};
    async function complete(payload) { var result=await mutate(payload); await load(); notice(effects[result.effect] || t('操作完成。','Done.')); }
    function closeModal() { if (modal && !busy) { modal.remove(); modal=null; } }
    function showModal(title, children, submit, label) {
        closeModal();
        var errorBox=element('div',{className:'easy-notice error',hidden:true});
        var submitButton=element('button',{type:'submit',className:'cbi-button cbi-button-apply',text:label || t('保存','Save')});
        var form=element('form',{},[element('h3',{id:'easy-modal-title',text:title})].concat(children,[errorBox,element('div',{className:'easy-buttons'},[button(t('取消','Cancel'),closeModal),submitButton])]));
        form.addEventListener('submit',async function(event){
            event.preventDefault(); if (busy) return;
            errorBox.hidden=true;
            try { await submit(); closeModal(); } catch(error) { errorBox.textContent=errorText(error); errorBox.hidden=false; }
        });
        modal=element('div',{className:'easy-modal-backdrop'},[element('div',{className:'easy-modal',role:'dialog','aria-modal':'true','aria-labelledby':'easy-modal-title'},[form])]);
        modal.addEventListener('keydown',function(event){
            if (event.key==='Escape') closeModal();
            if (event.key==='Tab') {
                var fields=Array.from(modal.querySelectorAll('input,button,select,textarea')).filter(function(n){return !n.disabled && !n.hidden;});
                var first=fields[0], last=fields[fields.length-1];
                if (event.shiftKey && document.activeElement===first) {event.preventDefault();last.focus();}
                else if (!event.shiftKey && document.activeElement===last) {event.preventDefault();first.focus();}
            }
        });
        root.appendChild(modal);
        var first=form.querySelector('input:not([type="checkbox"]),select,button'); if(first)first.focus();
    }
    function confirm(title, description, payload) { showModal(title,[element('p',{text:description})],function(){return complete(payload);},t('确认','Confirm')); }
    function field(label,input,help,wide) {
        if (!input.id) input.id='easy-field-'+Math.random().toString(36).slice(2);
        return element('div',{className:'easy-field'+(wide?' wide':'')},[element('label',{for:input.id,text:label}),input].concat(help?[element('small',{className:'easy-muted',text:help})]:[]));
    }
    function editUser(user) {
        var name=element('input',{value:user?user.name:'',maxlength:64,required:true,autocomplete:'off',pattern:'[A-Za-z0-9][A-Za-z0-9_.@\\-]*'});
        var password=element('input',{type:'password',autocomplete:'new-password',minlength:8,maxlength:128,required:!user});
        var group=element('input',{value:user?user.group:'*',maxlength:128});
        var enabled=element('input',{type:'checkbox',checked:!user || user.enabled});
        var reveal=element('input',{type:'checkbox',onChange:function(){password.type=reveal.checked?'text':'password';}});
        var fields=element('div',{className:'easy-fields'},[
            field(t('登录账号','Username'),name,t('支持英文、数字及 . _ - @','Letters, digits and . _ - @ are supported.')),
            field(user?t('新密码','New password'):t('密码','Password'),password,user?t('留空保留原密码。','Leave blank to keep the current password.'):t('至少 8 个字符。','At least 8 characters.')),
            field(t('用户组','Group'),group,t('通常保持 *。','Normally leave this as *.')),
            element('div',{className:'easy-field'},[element('label',{className:'easy-check'},[enabled,t('允许此账号登录','Allow this account to sign in')]),element('label',{className:'easy-check'},[reveal,t('显示新密码','Show new password')])])
        ]);
        showModal(user?t('修改账号','Edit account'):t('添加账号','Add account'),[fields,element('p',{className:'easy-muted',text:t('修改密码、账号名称、用户组或停用账号时，会使该账号的旧登录失效。','Changing the password, username, group or disabling an account revokes its previous sessions.')})],async function(){
            await complete({action:'save_user',id:user?user.id:'',name:name.value,group:group.value || '*',password:password.value,enabled:enabled.checked});
            password.value='';
        });
    }
    function table(headers,rows) {
        return element('div',{className:'easy-scroll'},[element('table',{},[element('thead',{},[element('tr',{},headers.map(function(h){return element('th',{text:h});}))]),element('tbody',{},rows.length?rows:[element('tr',{},[element('td',{colspan:headers.length,className:'easy-muted',text:t('暂无记录','No records')})])])])]);
    }
    function renderUsers() {
        var search=element('input',{type:'search',placeholder:t('搜索账号','Search accounts'),'aria-label':t('搜索账号','Search accounts')});
        var list=element('div',{});
        function rows() {
            list.replaceChildren(table([t('登录账号','Username'),t('用户组','Group'),t('状态','Status'),t('操作','Actions')],array(state.users).filter(function(user){return user.name.toLowerCase().includes(search.value.toLowerCase());}).map(function(user){
                var toggle=mutationButton(user.enabled?t('停用','Disable'):t('恢复','Enable'),function(){
                    confirm(user.enabled?t('停用账号','Disable account'):t('恢复账号','Enable account'),user.name+' — '+(user.enabled?t('此账号将无法登录，现有登录也会失效。','This account will be blocked and existing sessions revoked.'):t('恢复后可使用原密码登录。','The existing password will work again.')),{action:'save_user',id:user.id,name:user.name,group:user.group,password:'',enabled:!user.enabled});
                });
                return element('tr',{},[element('td',{text:user.name}),element('td',{text:user.group}),element('td',{className:user.enabled?'easy-good':'easy-muted',text:user.enabled?t('已启用','Enabled'):t('已停用','Disabled')}),element('td',{},[element('div',{className:'easy-buttons'},[
                    mutationButton(t('修改','Edit'),function(){editUser(user);}),toggle,
                    mutationButton(t('删除','Delete'),function(){confirm(t('删除账号','Delete account'),t('删除账号“','Delete account "')+user.name+t('”？该账号的旧登录将失效。','"? Previous sessions will be revoked.'),{action:'delete_user',id:user.id});},'cbi-button-remove')
                ])])]);
            })));
        }
        search.addEventListener('input',rows); rows();
        content.appendChild(element('section',{className:'easy-card'},[element('div',{className:'easy-heading'},[element('h3',{text:t('登录账号','Accounts')}),mutationButton(t('＋ 添加账号','＋ Add account'),function(){editUser(null);},'cbi-button-add')]),search,list]));
        var online=element('section',{className:'easy-card',id:'easy-online'}); content.appendChild(online); renderOnline();
    }
    function renderOnline() {
        var online=document.getElementById('easy-online'); if(!online)return;
        online.replaceChildren(element('h3',{text:t('在线连接','Online connections')}),table([t('账号','Username'),t('来源 IP','Remote IP'),t('VPN IP','VPN IP'),t('连接时间','Connected'),t('操作','Actions')],array(state.online).map(function(row){
            return element('tr',{},[element('td',{text:row.name}),element('td',{text:row.ip}),element('td',{text:row.vpn_ip}),element('td',{text:row.since || row.state}),element('td',{},[mutationButton(t('断开','Disconnect'),function(){confirm(t('断开连接','Disconnect'),row.name+' — '+t('仅断开此连接；如需禁止登录，请停用账号。','Disconnect this connection. Disable the account to prevent sign-in.'),{action:'disconnect',id:row.id});})])]);
        })));
        if(state.online_error)online.appendChild(element('p',{className:'easy-bad',text:t('无法读取在线列表，请检查 occtl。','Cannot read the online list. Check occtl.')}));
    }
    function renderSettings() {
        var s=state.settings, inputs={};
        function textInput(key,label,help,attributes) {
            var input=element('input',Object.assign({value:s[key] || ''},attributes || {})); inputs[key]=input; return field(label,input,help);
        }
        function flag(key,label,help) {
            var input=element('input',{type:'checkbox',checked:s[key]==='1'}); inputs[key]=input;
            return element('div',{className:'easy-field'},[element('label',{className:'easy-check'},[input,label]),element('small',{className:'easy-muted',text:help || ''})]);
        }
        var general=element('div',{className:'easy-fields'},[
            textInput('port',t('服务端口（TCP）','Service port (TCP)'),t('使用 4443 可避开路由器 HTTPS 管理端口。','4443 avoids the router HTTPS administration port.'),{type:'number',min:1,max:65535,required:true}),
            textInput('max_clients',t('最大在线设备数','Maximum online devices'),null,{type:'number',min:1,max:4096,required:true}),
            textInput('max_same',t('每个账号最多在线设备数','Devices per account'),t('设为 1 可限制一个账号同时一台设备。','Set to 1 to allow one device per account.'),{type:'number',min:1,max:64,required:true}),
            textInput('dpd',t('断线检测间隔（秒）','Dead-peer detection (seconds)'),t('通常使用 60–120 秒。','60–120 seconds is typical.'),{type:'number',min:10,max:3600,required:true}),
            textInput('ipaddr',t('VPN IPv4 网段','VPN IPv4 network'),t('例如 10.77.0.0，避免与现有局域网重叠。','For example 10.77.0.0. Avoid overlap with existing LANs.'),{required:true}),
            textInput('netmask',t('子网掩码','Subnet mask'),t('例如 255.255.255.0 或 24。','For example 255.255.255.0 or 24.'),{required:true})
        ]);
        var dns=element('textarea',{value:array(s.dns).join('\n'),required:true}); inputs.dns=dns;
        var routeMode=element('select',{},[element('option',{value:'all',text:t('全部流量通过 VPN','All traffic through VPN')}),element('option',{value:'split',text:t('仅指定网段通过 VPN','Only selected networks')})]); routeMode.value=s.route_mode; inputs.route_mode=routeMode;
        var routes=element('textarea',{value:array(s.routes).join('\n'),placeholder:'192.168.19.0/24'}); inputs.routes=routes;
        var routesField=field(t('VPN 路由','VPN routes'),routes,t('每行一个网段，例如 192.168.19.0/24。','One network per line, such as 192.168.19.0/24.'),true);
        function routeVisibility(){routesField.hidden=routeMode.value!=='split';routes.required=routeMode.value==='split';} routeMode.addEventListener('change',routeVisibility); routeVisibility();
        general.append(field(t('DNS 服务器','DNS servers'),dns,t('每行一个 IP；使用本机 DNS 时填写隧道地址，例如 10.77.0.1。','One IP per line. For DNS on this router, use its tunnel address, such as 10.77.0.1.')),
            field(t('流量路由方式','Traffic routing'),routeMode),routesField);
        var advanced=element('details',{},[element('summary',{text:t('更多设置','More settings')}),element('div',{className:'easy-fields'},[
            flag('udp',t('启用 UDP 加速（推荐）','Enable UDP acceleration (recommended)'),t('UDP 不通时客户端可回退 TCP。','Clients can fall back to TCP when UDP is unavailable.')),
            textInput('udp_port',t('UDP 端口','UDP port'),t('留空表示与 TCP 端口相同。','Leave blank to use the TCP port.'),{type:'number',min:1,max:65535}),
            flag('predictable_ips',t('同账号优先分配相同 IP','Prefer the same IP for each account')),
            flag('cisco_compat',t('兼容 Cisco AnyConnect','Cisco AnyConnect compatibility')),
            flag('compression',t('启用压缩','Enable compression'),t('通常关闭，减少 CPU 开销。','Usually disabled to reduce CPU work.')),
            textInput('default_domain',t('默认域名','Default domain'),t('可留空。','Optional.')),
            textInput('ip6addr',t('VPN IPv6 地址池','VPN IPv6 pool'),t('可留空，例如 fd77::/64。','Optional, for example fd77::/64.')),
            textInput('easy_public_url',t('员工连接地址','Employee connection address'),t('供下载客户端配置使用。','Used when downloading client profiles.'),{placeholder:'https://vpn.example.com:4443'})
        ])]);
        var save=element('button',{type:'submit',className:'cbi-button cbi-button-apply',text:t('保存并应用','Save and apply'),disabled:!writable() || !state.settings_supported});
        var form=element('form',{},[general,advanced,element('p',{className:'easy-muted',text:t('保存前会检查配置。服务运行时，应用网络设置会短暂重启 VPN，现有连接需要重连。','Configuration is checked before saving. Applying networking changes to a running service restarts VPN; connected users will reconnect.')}),save]);
        form.addEventListener('submit',function(event){
            event.preventDefault();
            var values={}; Object.keys(inputs).forEach(function(key){var input=inputs[key];values[key]=input.type==='checkbox'?(input.checked?'1':'0'):input.value;});
            values.dns=dns.value.split(/\r?\n/).map(function(x){return x.trim();}).filter(Boolean);
            values.routes=routes.value.split(/\r?\n/).map(function(x){return x.trim();}).filter(Boolean);
            confirm(t('应用服务设置','Apply service settings'),state.running?t('会短暂重启 VPN，断开当前连接。确认应用这些设置？','This briefly restarts VPN and disconnects current users. Apply these settings?'):t('保存后在下次启动服务时生效。','Settings take effect on the next service start.'),{action:'settings',settings:values,allow_restart:true});
        });
        content.appendChild(element('section',{className:'easy-card'},[element('h3',{text:t('常用服务设置','Common service settings')}),form]));
        if(!state.settings_supported)content.appendChild(element('p',{className:'easy-notice error',text:t('当前使用自定义认证或代理 ARP，请在原服务设置页管理。','Custom authentication or proxy ARP is configured. Use the original service settings page.')}));
        content.appendChild(element('p',{className:'easy-muted',text:t('此页配置 VPN 服务；防火墙、端口转发和 OpenClash 规则仍在各自页面管理。','This page configures VPN. Manage firewall rules, port forwarding and OpenClash in their respective pages.')}));
    }
    function download(kind,server) {
        var form=element('form',{method:'POST',action:options.base+'/download',target:'_blank'},[
            element('input',{type:'hidden',name:'token',value:options.token}),element('input',{type:'hidden',name:'kind',value:kind}),element('input',{type:'hidden',name:'server',value:server})
        ]); document.body.appendChild(form); form.submit(); form.remove();
    }
    function renderExport() {
        var server=element('input',{value:state.settings.easy_public_url || '',type:'url',required:true,placeholder:'https://vpn.example.com:4443'});
        var profile=element('button',{type:'submit',className:'cbi-button cbi-button-apply',text:t('下载 .bvpn 连接配置','Download .bvpn profile'),disabled:!state.ca_available});
        var form=element('form',{},[field(t('员工实际连接的服务器地址','Server address used by employees'),server,t('填写 HTTPS 地址，并确保服务器证书包含此域名或 IP。','Use HTTPS and ensure the server certificate covers this hostname or IP.')),element('p',{text:t('员工导入一个文件即可设置服务器地址和 CA；配置不包含密码或私钥。','Employees import one file to set the server and CA. It contains no passwords or private keys.')}),profile]);
        form.addEventListener('submit',function(event){event.preventDefault();download('profile',server.value);});
        var ca=button(t('下载 ca.pem','Download ca.pem'),function(){download('ca','');});ca.disabled=!state.ca_available;
        content.appendChild(element('section',{className:'easy-card'},[element('h3',{text:t('分发客户端配置','Distribute client profiles')}),form]));
        content.appendChild(element('section',{className:'easy-card'},[element('h3',{text:t('单独下载 CA 证书','Download the CA certificate')}),element('p',{className:'easy-muted',text:t('适用于已有服务器地址的布利杰VPN，或其他支持导入 CA 的客户端。','For BulijieVPN with a configured server address, or another client that can import a CA.')}),ca]));
    }
    function renderStatus() {
        statusBox.replaceChildren();
        [[t('VPN 服务','VPN service'),state.running?t('运行中','Running'):t('已停止','Stopped'),state.running?'easy-good':'easy-muted'],[t('在线连接','Online connections'),state.online_error?'—':String(array(state.online).length),''],[t('登录账号','Accounts'),String(array(state.users).length),'']].forEach(function(item){statusBox.appendChild(element('div',{className:'easy-card'},[element('span',{className:'easy-muted',text:item[0]}),element('strong',{className:item[2],text:item[1]})]));});
        renderOnline();
    }
    function renderTab() {
        content.replaceChildren();
        if(activeTab==='users')renderUsers(); else if(activeTab==='settings')renderSettings(); else renderExport();
        root.querySelectorAll('[role="tab"]').forEach(function(b){b.setAttribute('aria-selected',String(b.dataset.tab===activeTab));});
    }
    function render() {
        if(modal)modal.remove(); modal=null; root.replaceChildren();
        var actions=element('div',{className:'easy-buttons'},[
            button(t('刷新','Refresh'),function(){load().catch(function(e){notice(errorText(e),true);});}),
            mutationButton(state.running?t('停止服务','Stop service'):t('启动服务','Start service'),function(){
                confirm(state.running?t('停止 VPN','Stop VPN'):t('启动 VPN','Start VPN'),state.running?t('所有 VPN 连接将断开。','All VPN connections will close.'):t('使用已保存的配置启动服务。','Start the service with saved settings.'),{action:'service',command:state.running?'stop':'start',allow_restart:true});
            }),
            mutationButton(state.autostart?t('关闭开机启动','Disable autostart'):t('开机自动启动','Enable autostart'),function(){complete({action:'service',command:state.autostart?'disable':'enable'}).catch(function(e){notice(errorText(e),true);});})
        ]);
        root.appendChild(element('div',{className:'easy-heading'},[element('div',{},[element('h2',{text:'布利杰VPN'}),element('span',{className:'easy-muted',text:t('服务端管理','Server administration')+' · ocserv '+state.version})]),actions]));
        messageBox=element('div',{className:'easy-notice',hidden:true});root.appendChild(messageBox);
        statusBox=element('div',{className:'easy-summary'});root.appendChild(statusBox);renderStatus();
        if(!state.supported)notice(errors.upgrade_required,true);
        else if(!options.writable)notice(errors.forbidden);
        var tabs=element('div',{className:'easy-tabs',role:'tablist'});
        [['users',t('账号与在线用户','Accounts and connections')],['settings',t('服务设置','Service settings')],['export',t('客户端配置','Client profiles')]].forEach(function(item){var b=button(item[1],function(){activeTab=item[0];renderTab();});b.dataset.tab=item[0];b.setAttribute('role','tab');tabs.appendChild(b);});
        root.appendChild(tabs);content=element('div',{});root.appendChild(content);renderTab();
    }
    async function load() { state=await request('data'); render(); }
    var statusPending=false;
    setInterval(async function(){
        if(!state || busy || modal || document.hidden || statusPending)return;
        statusPending=true;
        try { var current=await request('status');state.running=current.running;state.online=current.online;state.online_error=current.online_error;renderStatus(); }
        catch(_){ /* Explicit refresh shows errors; background polling never interrupts typing. */ }
        finally {statusPending=false;}
    },10000);
    root.textContent=t('正在读取服务配置…','Loading service settings…');
    load().catch(function(error){root.replaceChildren(element('div',{className:'easy-notice error',text:errorText(error)}));});
})();
