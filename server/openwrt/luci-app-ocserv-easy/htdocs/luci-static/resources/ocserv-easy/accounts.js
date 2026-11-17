/* SPDX-License-Identifier: GPL-3.0-or-later */
window.OcservEasyAccounts = function ({ t, element, field, showModal, complete }) {
  "use strict";
  function editUser(user) {
    var name = element("input", {
      value: user ? user.name : "",
      maxlength: 64,
      required: true,
      autocomplete: "off",
      pattern: "[A-Za-z0-9][A-Za-z0-9_.@\\-]*",
    });
    var needsPassword = !user || !!user.needs_password;
    var password = element("input", {
      type: "password",
      autocomplete: "new-password",
      minlength: 8,
      maxlength: 128,
      required: needsPassword,
    });
    var repeat = element("input", {
      type: "password",
      autocomplete: "new-password",
      maxlength: 128,
      required: needsPassword,
    });
    var group = element("input", {
      value: user ? user.group : "*",
      maxlength: 128,
    });
    var enabled = element("input", {
      type: "checkbox",
      checked: !user || user.enabled || user.needs_password,
    });
    var reveal = element("input", {
      type: "checkbox",
      onChange: function () {
        password.type = repeat.type = reveal.checked ? "text" : "password";
      },
    });
    var fields = element("div", { className: "easy-fields" }, [
      field(
        t("登录账号", "Username"),
        name,
        t(
          "支持英文、数字及 . _ - @",
          "Letters, digits and . _ - @ are supported.",
        ),
        true,
      ),
      field(
        user ? t("新密码", "New password") : t("密码", "Password"),
        password,
        needsPassword
          ? t(
              "至少 8 个字符，保存后即可用于登录。",
              "At least 8 characters. Ready for login after saving.",
            )
          : t("留空保留原密码。", "Leave blank to keep the current password."),
      ),
      field(t("再次输入密码", "Confirm password"), repeat),
      element("div", { className: "easy-field wide" }, [
        element("label", { className: "easy-check" }, [
          enabled,
          t("允许此账号登录", "Allow this account to sign in"),
        ]),
        element("label", { className: "easy-check" }, [
          reveal,
          t("显示新密码", "Show new password"),
        ]),
      ]),
    ]);
    var advanced = element("details", {}, [
      element("summary", { text: t("用户组（可选）", "Group (optional)") }),
      field(
        t("用户组", "Group"),
        group,
        t("不需要分组时保持 *。", "Leave * unless you need groups."),
      ),
    ]);
    showModal(
      user ? t("修改账号", "Edit account") : t("添加账号", "Add account"),
      [
        fields,
        advanced,
        element("p", {
          className: "easy-muted",
          text: t(
            "修改密码、账号名称、用户组或停用账号时，会使该账号的旧登录失效。",
            "Changing the password, username, group or disabling an account revokes its previous sessions.",
          ),
        }),
      ],
      async function () {
        if (password.value !== repeat.value)
          throw { code: "password_mismatch" };
        await complete({
          action: "save_user",
          id: user ? user.id : "",
          name: name.value,
          group: group.value || "*",
          password: password.value,
          enabled: enabled.checked,
        });
        password.value = repeat.value = "";
      },
      t("保存并生效", "Save and activate"),
    );
  }
  return editUser;
};
