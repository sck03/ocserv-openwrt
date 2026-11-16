/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Static maintenance guidance: no network requests, timers or backend state. */
(function () {
    'use strict';
    window.LansyraMaintenance = function (element, button, t) {
        var command='lansyra-uninstall --yes';
        var input=element('input',{value:command,readonly:'readonly','aria-label':t('卸载命令','Uninstall command'),spellcheck:'false'});
        var status=element('p',{className:'easy-muted',role:'status'});
        var copy=button(t('复制卸载命令','Copy uninstall command'),async function(){
            copy.disabled=true;
            try {
                await navigator.clipboard.writeText(command);
                status.textContent=t('已复制。在路由器的 root SSH 终端执行。','Copied. Run it in the router’s root SSH terminal.');
            } catch (_) {
                input.focus(); input.select();
                status.textContent=t('命令已选中，请按 Ctrl+C 或长按复制。','Command selected. Press Ctrl+C or long-press to copy.');
            } finally { copy.disabled=false; }
        },'cbi-button-remove');
        return element('section',{className:'easy-card easy-maintenance'},[
            element('div',{className:'easy-heading'},[element('div',{},[
                element('span',{className:'easy-eyebrow',text:t('卸载与重新部署','REMOVE AND REINSTALL')}),
                element('h3',{text:t('清理服务端，重新开始','Start with a clean server')})
            ]),element('span',{className:'easy-maintenance-label',text:t('自动备份','Automatic backup')})]),
            element('p',{text:t('通过局域网 SSH 执行一条命令，完成服务端卸载。所有 VPN 连接会断开。','Run one command over LAN SSH to uninstall the server. All VPN connections will close.')}),
            element('ol',{className:'easy-maintenance-steps'},[
                element('li',{text:t('恢复 VPN 专用上网设置，备份当前配置。','Restore VPN-only settings and back up the configuration.')}),
                element('li',{text:t('停止服务，清理 VPN 规则和软件包。','Stop services and remove VPN rules and packages.')}),
                element('li',{text:t('删除账号和证书；重新安装后重新配置。','Delete accounts and certificates; configure them again after reinstalling.')})
            ]),
            element('div',{className:'easy-command'},[input,copy]),status,
            element('p',{className:'easy-muted',text:t('只查看卸载计划：lansyra-uninstall --dry-run。旧安装未提供此命令时，完整解压新套装并执行 sh uninstall.sh --yes。','Preview with lansyra-uninstall --dry-run. If the command is unavailable, extract the new bundle and run sh uninstall.sh --yes.')}),
            element('p',{className:'easy-muted',text:t('备份保存在 /root/lansyra-before-uninstall-*。代理订阅、其他网络规则和外部证书保留；不迁移旧数据。','Backups are kept in /root/lansyra-before-uninstall-*. Proxy subscriptions, other network rules and external certificates are retained. Old data is not migrated.')})
        ]);
    };
})();
