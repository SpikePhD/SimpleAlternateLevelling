// Simple Alternate Levelling - transactional skill allocation UI (AS2)

Stage.scaleMode = "showAll";
Stage.align = "";

var g_skillData = [];
var g_remainingPoints = 0;
var g_carryOver = 0;
var g_skillCap = 200;
var g_levelTFs = {};
var g_headerTF = undefined;
var g_controls = [];
var g_selected = 0;
var g_closing = false;
var g_pointsLabel = "Skill points to distribute:";
var g_confirmLabel = "Confirm";
var g_resetLabel = "Reset";

var PANEL_W = 820;
var PANEL_H = 0;
var PANEL_Y_OFFSET = -90;
var ROW_H = 36;
var COLUMN_GAP = 22;
var LABEL_VALUE_GAP = 4;
var VALUE_ARROW_GAP = 2;
var BUTTON_TOP_GAP = 18;
var BUTTON_ROW_OFFSET = 12;
var BUTTON_GAP = 16;
var FONT_SIZE = 13;
var HDR_FONT_SIZE = 16;

var ROW_START_Y = 54;
var BUTTON_H = 34;
var AUTO_HEIGHT_BOTTOM_PAD = 22;
var BTN_W = 28;
var BTN_H = 28;
var COL_X = [20, 312, 599];

var COLOR_GOLD = 0xC8B878;
var COLOR_BRIGHT = 0xFFD700;
var COLOR_DISABLED = 0x777777;
var COLOR_SELECTED = 0xFFFFBB;

function EA_Init(skillData, totalPoints, carryOver, panelW, panelH, panelYOffset, rowGap, columnGap, labelValueGap, valueArrowGap, buttonTopGap, buttonRowOffset, buttonGap, fontSize, headerFontSize, pointsLabel, confirmLabel, resetLabel) {
    g_skillData = skillData;
    g_remainingPoints = totalPoints;
    g_carryOver = carryOver;
    g_closing = false;
    if (g_skillData.length > 0 && g_skillData[0].skillCap != undefined) {
        g_skillCap = g_skillData[0].skillCap;
    }
    for (var originalIndex = 0; originalIndex < g_skillData.length; originalIndex++) {
        g_skillData[originalIndex].originalLevel = g_skillData[originalIndex].currentLevel;
    }

    if (panelW != undefined) { PANEL_W = panelW; }
    if (panelH != undefined) { PANEL_H = panelH; }
    if (panelYOffset != undefined) { PANEL_Y_OFFSET = panelYOffset; }
    if (rowGap != undefined) { ROW_H = rowGap; }
    if (columnGap != undefined) { COLUMN_GAP = columnGap; }
    if (labelValueGap != undefined) { LABEL_VALUE_GAP = labelValueGap; }
    if (valueArrowGap != undefined) { VALUE_ARROW_GAP = valueArrowGap; }
    if (buttonTopGap != undefined) { BUTTON_TOP_GAP = buttonTopGap; }
    if (buttonRowOffset != undefined) { BUTTON_ROW_OFFSET = buttonRowOffset; }
    if (buttonGap != undefined) { BUTTON_GAP = buttonGap; }
    if (fontSize != undefined) { FONT_SIZE = fontSize; }
    if (headerFontSize != undefined) { HDR_FONT_SIZE = headerFontSize; }
    if (pointsLabel != undefined && pointsLabel.length > 0) { g_pointsLabel = pointsLabel; }
    if (confirmLabel != undefined && confirmLabel.length > 0) { g_confirmLabel = confirmLabel; }
    if (resetLabel != undefined && resetLabel.length > 0) { g_resetLabel = resetLabel; }

    if (PANEL_H <= 0) { PANEL_H = _measureAutoPanelHeight(g_skillData); }
    COL_X[0] = 20;
    COL_X[1] = 290 + COLUMN_GAP;
    COL_X[2] = 555 + COLUMN_GAP * 2;
    _buildPanel();
    _selectControl(0);
}

function EA_UpdateSkill(actorValue, newLevel) {
    var tf = g_levelTFs[actorValue];
    if (tf != undefined) { tf.text = String(Math.floor(newLevel)); }
    for (var i = 0; i < g_skillData.length; i++) {
        if (g_skillData[i].actorValue == actorValue) {
            g_skillData[i].currentLevel = newLevel;
            break;
        }
    }
    _refreshControls();
}

function EA_UpdatePoints(remaining) {
    g_remainingPoints = remaining;
    _refreshHeader();
    _refreshControls();
}

function EA_SetClosing() {
    g_closing = true;
    _refreshControls();
}

function _makeFmt(size, bold, color, align) {
    var fmt = new TextFormat();
    fmt.font = "$EverywhereMediumFont";
    fmt.size = size;
    fmt.bold = bold;
    fmt.color = color;
    if (align != undefined) { fmt.align = align; }
    return fmt;
}

function _applyFmt(tf, fmt) {
    tf.setNewTextFormat(fmt);
    tf.setTextFormat(fmt);
}

function _refreshHeader() {
    if (g_headerTF != undefined) {
        g_headerTF.text = g_pointsLabel + "  " + g_remainingPoints;
    }
}

function _measureAutoPanelHeight(skillData) {
    var maxRow = 0;
    for (var i = 0; i < skillData.length; i++) {
        if (skillData[i].row > maxRow) { maxRow = skillData[i].row; }
    }
    var buttonY = ROW_START_Y + (maxRow + 1) * ROW_H + BUTTON_TOP_GAP + BUTTON_ROW_OFFSET;
    return buttonY + BUTTON_H + AUTO_HEIGHT_BOTTOM_PAD;
}

function _buildPanel() {
    _root.panelMC.removeMovieClip();
    g_levelTFs = {};
    g_controls = [];

    var SW = (Stage.width > 0) ? Stage.width : 1280;
    var SH = (Stage.height > 0) ? Stage.height : 720;
    var pX = Math.floor((SW - PANEL_W) / 2);
    var pY = Math.floor((SH - PANEL_H) / 2) + PANEL_Y_OFFSET;
    pX = Math.max(0, Math.min(pX, Math.max(0, SW - PANEL_W)));
    pY = Math.max(0, Math.min(pY, Math.max(0, SH - PANEL_H)));

    var p = _root.createEmptyMovieClip("panelMC", 10);
    p._x = pX;
    p._y = pY;
    p.beginFill(0x000000, 65);
    p.moveTo(0, 0); p.lineTo(PANEL_W, 0); p.lineTo(PANEL_W, PANEL_H); p.lineTo(0, PANEL_H); p.endFill();
    p.lineStyle(1, COLOR_GOLD, 50);
    p.moveTo(0, 0); p.lineTo(PANEL_W, 0); p.lineTo(PANEL_W, PANEL_H); p.lineTo(0, PANEL_H); p.lineTo(0, 0);

    p.createTextField("headerTF", 1, 10, 12, PANEL_W - 20, 28);
    g_headerTF = p["headerTF"];
    g_headerTF.selectable = false;
    _applyFmt(g_headerTF, _makeFmt(HDR_FONT_SIZE, true, COLOR_BRIGHT, "center"));
    _refreshHeader();

    var depth = 100;
    for (var i = 0; i < g_skillData.length; i++) {
        var sk = g_skillData[i];
        var rx = COL_X[sk.column];
        var ry = ROW_START_Y + sk.row * ROW_H;
        p.createTextField("sn" + i, depth++, rx, ry + 4, 146, 24);
        var nameTF = p["sn" + i];
        nameTF.selectable = false;
        _applyFmt(nameTF, _makeFmt(FONT_SIZE, false, COLOR_GOLD));
        nameTF.text = sk.name;

        p.createTextField("sl" + i, depth++, rx + 146 + LABEL_VALUE_GAP, ry + 4, 34, 24);
        var levelTF = p["sl" + i];
        levelTF.selectable = false;
        _applyFmt(levelTF, _makeFmt(FONT_SIZE, false, COLOR_GOLD, "right"));
        levelTF.text = String(Math.floor(sk.currentLevel));
        g_levelTFs[sk.actorValue] = levelTF;

        var arrow = p.createEmptyMovieClip("btn" + i, depth++);
        arrow._x = rx + 146 + LABEL_VALUE_GAP + 34 + VALUE_ARROW_GAP;
        arrow._y = ry + 2;
        arrow.kind = "skill";
        arrow.skillIndex = i;
        arrow.actorValue = sk.actorValue;
        g_controls.push(arrow);
        _wireControl(arrow, i);
    }

    var btnW = 140;
    var totalBtnW = btnW * 2 + BUTTON_GAP;
    var btnY = ROW_START_Y + 6 * ROW_H + BUTTON_TOP_GAP + BUTTON_ROW_OFFSET;
    var btnStartX = Math.floor((PANEL_W - totalBtnW) / 2);

    var reset = p.createEmptyMovieClip("resetMC", depth++);
    reset._x = btnStartX; reset._y = btnY; reset.kind = "reset";
    g_controls.push(reset); _wireControl(reset, g_controls.length - 1);

    var confirm = p.createEmptyMovieClip("confirmMC", depth++);
    confirm._x = btnStartX + btnW + BUTTON_GAP; confirm._y = btnY; confirm.kind = "confirm";
    g_controls.push(confirm); _wireControl(confirm, g_controls.length - 1);
    _refreshControls();
}

function _wireControl(mc, index) {
    mc.controlIndex = index;
    mc.useHandCursor = true;
    mc.onRollOver = function() { _selectControl(this.controlIndex); };
    mc.onRelease = function() { _activateControl(this.controlIndex); };
}

function _hasChanges() {
    for (var i = 0; i < g_skillData.length; i++) {
        if (g_skillData[i].currentLevel != g_skillData[i].originalLevel) { return true; }
    }
    return false;
}

function _isDisabled(control) {
    if (g_closing) { return true; }
    if (control.kind == "skill") {
        return g_remainingPoints <= 0 || g_skillData[control.skillIndex].currentLevel >= g_skillCap;
    }
    if (control.kind == "reset") { return !_hasChanges(); }
    return false;
}

function _refreshControls() {
    for (var i = 0; i < g_controls.length; i++) { _drawControl(g_controls[i], i == g_selected); }
}

function _selectControl(index) {
    if (index < 0 || index >= g_controls.length) { return; }
    g_selected = index;
    _refreshControls();
}

function _drawControl(mc, selected) {
    var disabled = _isDisabled(mc);
    mc.disabled = disabled;
    mc.useHandCursor = !disabled;
    var lineColor = disabled ? COLOR_DISABLED : (selected ? COLOR_SELECTED : COLOR_GOLD);
    var fillAlpha = disabled ? 18 : (selected ? 65 : 35);
    var width = (mc.kind == "skill") ? BTN_W : 140;
    mc.clear();
    mc.beginFill(0x333333, fillAlpha);
    mc.moveTo(0, 0); mc.lineTo(width, 0); mc.lineTo(width, (mc.kind == "skill") ? BTN_H : BUTTON_H);
    mc.lineTo(0, (mc.kind == "skill") ? BTN_H : BUTTON_H); mc.endFill();
    mc.lineStyle(selected ? 2 : 1, lineColor, disabled ? 40 : 90);
    mc.moveTo(0, 0); mc.lineTo(width, 0); mc.lineTo(width, (mc.kind == "skill") ? BTN_H : BUTTON_H);
    mc.lineTo(0, (mc.kind == "skill") ? BTN_H : BUTTON_H); mc.lineTo(0, 0);

    if (mc.kind == "skill") {
        var cx = BTN_W / 2; var cy = BTN_H / 2;
        mc.lineStyle(2, lineColor, disabled ? 40 : 100);
        mc.moveTo(cx - 3, cy - 5); mc.lineTo(cx + 4, cy); mc.lineTo(cx - 3, cy + 5);
    } else {
        if (mc["lbl"] == undefined) {
            mc.createTextField("lbl", 0, 0, 6, 140, 22);
            mc["lbl"].selectable = false;
        }
        _applyFmt(mc["lbl"], _makeFmt(FONT_SIZE + 1, true, disabled ? COLOR_DISABLED : COLOR_BRIGHT, "center"));
        mc["lbl"].text = (mc.kind == "reset") ? g_resetLabel : g_confirmLabel;
    }
}

function _activateControl(index) {
    if (g_closing || index < 0 || index >= g_controls.length) { return; }
    var control = g_controls[index];
    if (_isDisabled(control)) { return; }
    if (control.kind == "skill") {
        gfx.io.GameDelegate.call("EA_OnAllocate", [control.actorValue]);
    } else if (control.kind == "reset") {
        gfx.io.GameDelegate.call("EA_OnReset", []);
    } else if (control.kind == "confirm") {
        g_closing = true;
        _refreshControls();
        gfx.io.GameDelegate.call("EA_OnConfirm", []);
    }
}

function _findSkillControl(column, row) {
    for (var i = 0; i < 18; i++) {
        var sk = g_skillData[g_controls[i].skillIndex];
        if (sk.column == column && sk.row == row) { return i; }
    }
    return -1;
}

function _moveGrid(keyCode) {
    var control = g_controls[g_selected];
    if (control.kind != "skill") {
        if (keyCode == Key.LEFT || keyCode == Key.RIGHT) { _selectControl(g_selected == 18 ? 19 : 18); }
        else if (keyCode == Key.UP) { _selectControl(g_selected == 18 ? _findSkillControl(0, 5) : _findSkillControl(2, 5)); }
        return;
    }
    var sk = g_skillData[control.skillIndex];
    var column = sk.column; var row = sk.row;
    if (keyCode == Key.LEFT) { column = Math.max(0, column - 1); }
    if (keyCode == Key.RIGHT) { column = Math.min(2, column + 1); }
    if (keyCode == Key.UP) { row = Math.max(0, row - 1); }
    if (keyCode == Key.DOWN) {
        if (row == 5) { _selectControl(column < 2 ? 18 : 19); return; }
        row++;
    }
    var next = _findSkillControl(column, row);
    if (next >= 0) { _selectControl(next); }
}

var g_keyListener = {};
g_keyListener.onKeyDown = function() {
    if (g_closing || g_controls.length == 0) { return; }
    var code = Key.getCode();
    if (code == Key.ESCAPE || code == 27 || code == 67) { _activateControl(19); return; }
    if (code == 82) { _activateControl(18); return; }
    if (code == Key.ENTER || code == 13 || code == 32) { _activateControl(g_selected); return; }
    if (code == Key.TAB || code == 9) {
        var direction = Key.isDown(Key.SHIFT) ? -1 : 1;
        _selectControl((g_selected + direction + g_controls.length) % g_controls.length);
        return;
    }
    if (code == Key.LEFT || code == Key.RIGHT || code == Key.UP || code == Key.DOWN) { _moveGrid(code); }
};
Key.addListener(g_keyListener);
