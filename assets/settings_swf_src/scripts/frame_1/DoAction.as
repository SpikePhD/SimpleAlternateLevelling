// SAL Settings: all player-facing strings arrive from the translation file via native SAL_Init.
Stage.scaleMode = "showAll";
Stage.align = "";

var salRows = [];
var salSections = [];
var salActions = [];
var salPresets = [];
var salModes = [];
var salSection = 0;
var salPage = 0;
var salPageSize = 8;
var salError = "";
var salEditing = -1;
var salPanel = undefined;

function SAL_Init(rows, sections, actions, presets, modes) {
    salRows = rows;
    salSections = sections;
    salActions = actions;
    salPresets = presets;
    salModes = modes;
    salSection = 0;
    salPage = 0;
    salError = "";
    salDraw();
}

function SAL_Update(rows) {
    salRows = rows;
    salDraw();
}

function SAL_Error(message) {
    salError = message;
    salDraw();
}

function salFormat(size, color, align) {
    var fmt = new TextFormat();
    fmt.font = "$EverywhereMediumFont";
    fmt.size = size;
    fmt.color = color;
    fmt.align = align;
    return fmt;
}

function salText(parent, name, depth, x, y, w, h, text, size, color, align) {
    parent.createTextField(name, depth, x, y, w, h);
    var field = parent[name];
    field.selectable = false;
    field.text = text;
    var fmt = salFormat(size, color, align);
    field.setNewTextFormat(fmt);
    field.setTextFormat(fmt);
    return field;
}

function salBox(mc, w, h, selected, disabled) {
    mc.clear();
    mc.beginFill(selected ? 0x5A5038 : 0x24282B, disabled ? 18 : 75);
    mc.moveTo(0, 0); mc.lineTo(w, 0); mc.lineTo(w, h); mc.lineTo(0, h); mc.endFill();
    mc.lineStyle(1, selected ? 0xFFD700 : 0x918563, disabled ? 30 : 75);
    mc.moveTo(0, 0); mc.lineTo(w, 0); mc.lineTo(w, h); mc.lineTo(0, h); mc.lineTo(0, 0);
}

function salButton(parent, name, depth, x, y, w, h, label, selected) {
    var mc = parent.createEmptyMovieClip(name, depth);
    mc._x = x;
    mc._y = y;
    salBox(mc, w, h, selected, false);
    salText(mc, "text", 1, 4, 5, w - 8, h - 7, label, 13, selected ? 0xFFD700 : 0xE5D9B3, "center");
    mc.useHandCursor = true;
    return mc;
}

function salModeLabel(value) {
    if (value == "zero") return salModes[1];
    if (value == "uniform") return salModes[2];
    if (value == "custom") return salModes[3];
    return salModes[0];
}

function salVisibleRows() {
    var result = [];
    for (var i = 0; i < salRows.length; i++) {
        if (salRows[i].section == salSection) result.push(salRows[i]);
    }
    return result;
}

function salSendValue(row, value) {
    if (row.kind == 0 || row.kind == 1) {
        var number = Number(value);
        if (isNaN(number) || number < row.min || number > row.max ||
            (row.kind == 1 && Math.floor(number) != number) ||
            (row.key == "skill_allocation.panel_height" && number != 0 && number < 300)) return false;
        if (row.value == number) return true;
        row.value = number;
    }
    gfx.io.GameDelegate.call("SAL_OnSet", [row.index, String(value)]);
    return true;
}

function salDraw() {
    if (salPanel != undefined) salPanel.removeMovieClip();
    salPanel = _root.createEmptyMovieClip("salPanel", 10);
    salPanel._x = 64;
    salPanel._y = 30;
    salPanel.beginFill(0x080C10, 88);
    salPanel.moveTo(0, 0); salPanel.lineTo(1152, 0); salPanel.lineTo(1152, 660);
    salPanel.lineTo(0, 660); salPanel.endFill();
    salPanel.lineStyle(1, 0xC8B878, 90);
    salPanel.moveTo(0, 0); salPanel.lineTo(1152, 0); salPanel.lineTo(1152, 660);
    salPanel.lineTo(0, 660); salPanel.lineTo(0, 0);

    salText(salPanel, "title", 1, 20, 12, 600, 34, salActions[0], 23, 0xFFD700, "left");
    salText(salPanel, "hint", 2, 710, 19, 420, 22, salActions[9], 12, 0xC8B878, "right");
    if (salError.length > 0) salText(salPanel, "error", 3, 305, 46, 830, 22, salError, 14, 0xFF6666, "left");

    var depth = 20;
    for (var section = 0; section < salSections.length; section++) {
        var tab = salButton(salPanel, "section" + section, depth++, 18, 75 + section * 39,
                            254, 34, salSections[section], section == salSection);
        tab.sectionIndex = section;
        tab.onRelease = function() { salSection = this.sectionIndex; salPage = 0; salDraw(); };
    }

    for (var preset = 0; preset < salPresets.length; preset++) {
        var presetButton = salButton(salPanel, "preset" + preset, depth++,
            302 + (preset % 3) * 273, 72 + Math.floor(preset / 3) * 38,
            260, 32, salPresets[preset], false);
        presetButton.presetIndex = preset;
        presetButton.onRelease = function() { gfx.io.GameDelegate.call("SAL_OnPreset", [this.presetIndex]); };
    }

    var rows = salVisibleRows();
    var pages = Math.max(1, Math.ceil(rows.length / salPageSize));
    if (salPage >= pages) salPage = pages - 1;
    for (var visible = 0; visible < salPageSize; visible++) {
        var index = salPage * salPageSize + visible;
        if (index >= rows.length) break;
        var row = rows[index];
        var y = 167 + visible * 48;
        salText(salPanel, "label" + visible, depth++, 310, y + 9, 506, 26,
            row.label, 14, 0xE0D2A7, "left");
        if (row.kind == 2 || row.kind == 3) {
            var display = row.kind == 2 ? (row.value ? salActions[7] : salActions[8]) : salModeLabel(row.value);
            var toggle = salButton(salPanel, "toggle" + visible, depth++, 828, y, 294, 36, display, false);
            toggle.settingRow = row;
            toggle.onRelease = function() {
                if (this.settingRow.kind == 2) {
                    salSendValue(this.settingRow, this.settingRow.value ? "false" : "true");
                } else {
                    var options = ["vanilla", "zero", "uniform", "custom"];
                    var current = 0;
                    for (var m = 0; m < options.length; m++) if (options[m] == this.settingRow.value) current = m;
                    salSendValue(this.settingRow, options[(current + 1) % options.length]);
                }
            };
        } else {
            var minus = salButton(salPanel, "minus" + visible, depth++, 825, y, 35, 36, "-", false);
            minus.settingRow = row;
            minus.onRelease = function() {
                if (salSendValue(this.settingRow, Number(this.settingRow.value) - Number(this.settingRow.step))) salDraw();
            };
            var plus = salButton(salPanel, "plus" + visible, depth++, 1087, y, 35, 36, "+", false);
            plus.settingRow = row;
            plus.onRelease = function() {
                if (salSendValue(this.settingRow, Number(this.settingRow.value) + Number(this.settingRow.step))) salDraw();
            };
            salPanel.createTextField("value" + visible, depth++, 865, y + 5, 216, 30);
            var valueField = salPanel["value" + visible];
            valueField.type = "input";
            valueField.selectable = true;
            valueField.border = true;
            valueField.background = true;
            valueField.backgroundColor = 0x20282C;
            valueField.borderColor = 0x93865F;
            valueField.restrict = "0-9.\\-";
            valueField.maxChars = 12;
            valueField.text = String(row.value);
            valueField.settingRow = row;
            var fieldFormat = salFormat(16, 0xFFFFFF, "center");
            valueField.setNewTextFormat(fieldFormat);
            valueField.setTextFormat(fieldFormat);
            valueField.onSetFocus = function() { salEditing = this.settingRow.index; };
            valueField.onKillFocus = function() {
                salEditing = -1;
                if (!salSendValue(this.settingRow, this.text)) this.text = String(this.settingRow.value);
            };
        }
    }

    var previous = salButton(salPanel, "previous", depth++, 302, 554, 140, 32, salActions[5], false);
    previous.onRelease = function() { if (salPage > 0) { salPage--; salDraw(); } };
    salText(salPanel, "page", depth++, 626, 558, 160, 27,
            String(salPage + 1) + " / " + String(pages), 14, 0xE0D2A7, "center");
    var next = salButton(salPanel, "next", depth++, 982, 554, 140, 32, salActions[6], false);
    next.onRelease = function() { if (salPage + 1 < pages) { salPage++; salDraw(); } };

    var resetSection = salButton(salPanel, "resetSection", depth++, 302, 610, 185, 34, salActions[3], false);
    resetSection.onRelease = function() { gfx.io.GameDelegate.call("SAL_OnResetSection", [salSection]); };
    var resetAll = salButton(salPanel, "resetAll", depth++, 495, 610, 185, 34, salActions[4], false);
    resetAll.onRelease = function() { gfx.io.GameDelegate.call("SAL_OnResetAll", []); };
    var cancel = salButton(salPanel, "cancel", depth++, 732, 610, 185, 34, salActions[2], false);
    cancel.onRelease = function() { gfx.io.GameDelegate.call("SAL_OnCancel", []); };
    var apply = salButton(salPanel, "apply", depth++, 925, 610, 197, 34, salActions[1], true);
    apply.onRelease = function() { gfx.io.GameDelegate.call("SAL_OnApply", []); };
}

var salKeys = {};
salKeys.onKeyDown = function() {
    if (Key.getCode() == Key.ESCAPE || Key.getCode() == 27) {
        gfx.io.GameDelegate.call("SAL_OnCancel", []);
    } else if (Key.getCode() == Key.ENTER || Key.getCode() == 13) {
        if (salEditing >= 0) Selection.setFocus(null);
    }
};
Key.addListener(salKeys);
