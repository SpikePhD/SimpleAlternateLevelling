// EA_SkillMenu -- frame 1 root script.
// Global functions callable by C++ via uiMovie->InvokeNoReturn.
// AS2->C++ callbacks use gfx.io.GameDelegate.call (routed through FxDelegate).
// Type annotations are NOT valid in frame scripts -- omitted intentionally.

_global.gfxExtensions = true;
Shared.GlobalFunc.MaintainTextFormat();

// Hide ALL pre-existing MovieClips on the root timeline.
// The base SWF (trainingmenu.swf) places TrainingCard (DefineSprite 46)
// and TrainingMenuObj (DefineSprite 47) via PlaceObject2 tags — these
// cannot be removed, only hidden.
for (var _k in _root) {
    if (typeof _root[_k] == "movieclip") {
        _root[_k]._visible = false;
    }
}

// ---- Menu state ----
var g_skillData = [];
var g_remainingPoints = 0;
var g_carryOver = 0;
// actorValue -> TextField, for fast level display updates
var g_levelTFs = {};
// Header text field reference
var g_headerTF;

// ---- Panel geometry (pixels, 1280x720 canvas) - overridden by EA_Init ----
var PANEL_W = 820;
var PANEL_H = 360;
var PANEL_Y_OFFSET = -90;
var COLUMN_GAP = 22;
var LABEL_VALUE_GAP = 4;
var VALUE_ARROW_GAP = 2;
var BUTTON_TOP_GAP = 18;
var BUTTON_ROW_OFFSET = 12;
var BUTTON_GAP = 16;
var COL_X   = [20, 290, 555];   // column x offsets inside the panel
var ROW_START_Y = 50;
var ROW_H = 36;
var BTN_W = 26;
var BTN_H = 24;
var BUTTON_H = 30;
var AUTO_HEIGHT_BOTTOM_PAD = 18;

// ---- Font sizes - overridden by EA_Init ----
var FONT_SIZE     = 13;
var HDR_FONT_SIZE = 16;

// ---- Colors ----
var COLOR_GOLD   = 0xC8B878;   // borders, labels, skill names
var COLOR_BRIGHT = 0xFFD700;   // header, confirm/reset button text

// ====================================================================
// C++ -> AS2 API
// ====================================================================

function EA_Init(skillData, totalPoints, carryOver, panelW, panelH, panelYOffset, rowGap, columnGap, labelValueGap, valueArrowGap, buttonTopGap, buttonRowOffset, buttonGap, fontSize, headerFontSize) {
    g_skillData = skillData;
    g_remainingPoints = totalPoints;
    g_carryOver = carryOver;

    // Apply layout config from JSON (passed by C++).
    if (panelW != undefined && panelW > 0)          { PANEL_W = panelW; }
    if (panelH != undefined && panelH > 0)          { PANEL_H = panelH; }
    if (panelYOffset != undefined)                  { PANEL_Y_OFFSET = panelYOffset; }
    if (rowGap != undefined && rowGap > 0)          { ROW_H = rowGap; }
    if (columnGap != undefined)                    { COLUMN_GAP = columnGap; }
    if (labelValueGap != undefined)                { LABEL_VALUE_GAP = labelValueGap; }
    if (valueArrowGap != undefined)                { VALUE_ARROW_GAP = valueArrowGap; }
    if (buttonTopGap != undefined)                 { BUTTON_TOP_GAP = buttonTopGap; }
    if (buttonRowOffset != undefined)              { BUTTON_ROW_OFFSET = buttonRowOffset; }
    if (buttonGap != undefined && buttonGap > 0)   { BUTTON_GAP = buttonGap; }
    if (fontSize != undefined && fontSize > 0)       { FONT_SIZE = fontSize; }
    if (headerFontSize != undefined && headerFontSize > 0) { HDR_FONT_SIZE = headerFontSize; }

    // Auto-size height if 0 or not provided.
    if (PANEL_H <= 0) {
        PANEL_H = _measureAutoPanelHeight(g_skillData);
    }

    COL_X[0] = 20;
    COL_X[1] = 290 + COLUMN_GAP;
    COL_X[2] = 555 + COLUMN_GAP * 2;

    _buildPanel();
}

function EA_UpdateSkill(actorValue, newLevel) {
    var tf = g_levelTFs[actorValue];
    if (tf != undefined) {
        tf.text = String(Math.floor(newLevel));
    }
    for (var i = 0; i < g_skillData.length; i++) {
        if (g_skillData[i].actorValue == actorValue) {
            g_skillData[i].currentLevel = newLevel;
            break;
        }
    }
}

function EA_UpdatePoints(remaining) {
    g_remainingPoints = remaining;
    if (g_headerTF != undefined) {
        g_headerTF.text = "Skill points to distribute:  " + remaining;
    }
}

// ====================================================================
// TextFormat helper -- $EverywhereMediumFont is a registered GFx alias in the
// trainingmenu base SWF. Color/size/bold are explicit (no theme tokens).
// ====================================================================

function _makeFmt(size, bold, color, align) {
    var fmt = new TextFormat();
    fmt.font  = "$EverywhereMediumFont";
    fmt.size  = size;
    fmt.bold  = bold;
    fmt.color = color;
    if (align != undefined) { fmt.align = align; }
    return fmt;
}

function _applyFmt(tf, fmt) {
    tf.setNewTextFormat(fmt);
    tf.setTextFormat(fmt);
}

// ====================================================================
// UI construction
// ====================================================================

function _buildPanel() {
    _root.panelMC.removeMovieClip();
    g_levelTFs = {};

    var SW = (Stage.width  > 0) ? Stage.width  : 1280;
    var SH = (Stage.height > 0) ? Stage.height : 720;
    var pX = Math.floor((SW - PANEL_W) / 2);
    var pY = Math.floor((SH - PANEL_H) / 2) + PANEL_Y_OFFSET;

    var p = _root.createEmptyMovieClip("panelMC", 10);
    p._x = pX;
    p._y = pY;

    // Background
    p.beginFill(0x000000, 65);
    p.moveTo(0, 0);
    p.lineTo(PANEL_W, 0);
    p.lineTo(PANEL_W, PANEL_H);
    p.lineTo(0, PANEL_H);
    p.endFill();
    // Border
    p.lineStyle(1, COLOR_GOLD, 50);
    p.moveTo(0, 0);
    p.lineTo(PANEL_W, 0);
    p.lineTo(PANEL_W, PANEL_H);
    p.lineTo(0, PANEL_H);
    p.lineTo(0, 0);

    // Header
    p.createTextField("headerTF", 1, 10, 12, PANEL_W - 20, 28);
    g_headerTF = p["headerTF"];
    g_headerTF.selectable = false;
    _applyFmt(g_headerTF, _makeFmt(HDR_FONT_SIZE, true, COLOR_BRIGHT, "center"));
    g_headerTF.text = "Skill points to distribute:  " + g_remainingPoints;

    // Skill rows
    var depth = 100;
    for (var i = 0; i < g_skillData.length; i++) {
        var sk = g_skillData[i];
        var rx = COL_X[sk.column];
        var ry = ROW_START_Y + sk.row * ROW_H;

        // Name
        p.createTextField("sn" + i, depth++, rx, ry + 4, 146, 24);
        var nameTF = p["sn" + i];
        nameTF.selectable = false;
        _applyFmt(nameTF, _makeFmt(FONT_SIZE, false, COLOR_GOLD));
        nameTF.text = sk.name;

        // Level (right-aligned)
        p.createTextField("sl" + i, depth++, rx + 146 + LABEL_VALUE_GAP, ry + 4, 34, 24);
        var lvlTF = p["sl" + i];
        lvlTF.selectable = false;
        _applyFmt(lvlTF, _makeFmt(FONT_SIZE, false, COLOR_GOLD, "right"));
        lvlTF.text = String(Math.floor(sk.currentLevel));
        g_levelTFs[sk.actorValue] = lvlTF;

        // ">" button
        var btn = p.createEmptyMovieClip("btn" + i, depth++);
        btn._x = rx + 146 + LABEL_VALUE_GAP + 34 + VALUE_ARROW_GAP;
        btn._y = ry + 2;
        _makeArrowBtn(btn, sk.actorValue);
    }

    // Buttons row — Reset + Confirm, centered side by side
    var btnW = 140;
    var btnGap = BUTTON_GAP;
    var totalBtnW = btnW * 2 + btnGap;
    var btnY = ROW_START_Y + 6 * ROW_H + BUTTON_TOP_GAP + BUTTON_ROW_OFFSET;
    var btnStartX = Math.floor((PANEL_W - totalBtnW) / 2);

    var resetMC = p.createEmptyMovieClip("resetMC", depth++);
    resetMC._x = btnStartX;
    resetMC._y = btnY;
    _makeResetBtn(resetMC);

    var confirmMC = p.createEmptyMovieClip("confirmMC", depth++);
    confirmMC._x = btnStartX + btnW + btnGap;
    confirmMC._y = btnY;
    _makeConfirmBtn(confirmMC);
}

function _measureAutoPanelHeight(skillData) {
    var maxRow = 0;
    for (var i = 0; i < skillData.length; i++) {
        if (skillData[i].row > maxRow) {
            maxRow = skillData[i].row;
        }
    }

    var skillRows = maxRow + 1;
    var buttonY = ROW_START_Y + skillRows * ROW_H + BUTTON_TOP_GAP;
    return buttonY + BUTTON_H + AUTO_HEIGHT_BOTTOM_PAD;
}

// ====================================================================
// Arrow button
// ====================================================================

function _makeArrowBtn(mc, actorValue) {
    _drawBtn(mc, false);
    mc.actorValue = actorValue;
    mc.useHandCursor = true;
    mc.onRollOver = function() { _drawBtn(this, true); };
    mc.onRollOut  = function() { _drawBtn(this, false); };
    mc.onRelease  = function() {
        gfx.io.GameDelegate.call("EA_OnAllocate", [this.actorValue]);
    };
}

function _drawBtn(mc, hover) {
    mc.clear();
    var bgAlpha   = hover ? 55 : 30;
    var lineAlpha = hover ? 80 : 50;
    var lineColor = hover ? 0xFFFFBB : COLOR_GOLD;
    mc.beginFill(0x888888, bgAlpha);
    mc.moveTo(0, 0);
    mc.lineTo(BTN_W, 0);
    mc.lineTo(BTN_W, BTN_H);
    mc.lineTo(0, BTN_H);
    mc.endFill();
    mc.lineStyle(1, lineColor, lineAlpha);
    mc.moveTo(0, 0);
    mc.lineTo(BTN_W, 0);
    mc.lineTo(BTN_W, BTN_H);
    mc.lineTo(0, BTN_H);
    mc.lineTo(0, 0);
    var cx = BTN_W / 2;
    var cy = BTN_H / 2;
    mc.lineStyle(2, lineColor, hover ? 100 : 80);
    mc.moveTo(cx - 3, cy - 5);
    mc.lineTo(cx + 4, cy);
    mc.lineTo(cx - 3, cy + 5);
}

// ====================================================================
// Confirm button
// ====================================================================

function _makeConfirmBtn(mc) {
    _drawConfirm(mc, false);
    mc.useHandCursor = true;
    mc.onRollOver = function() { _drawConfirm(this, true); };
    mc.onRollOut  = function() { _drawConfirm(this, false); };
    mc.onRelease  = function() {
        gfx.io.GameDelegate.call("EA_OnConfirm", []);
    };
}

function _drawConfirm(mc, hover) {
    mc.clear();
    mc.beginFill(0x333333, hover ? 75 : 55);
    mc.moveTo(0, 0);
    mc.lineTo(140, 0);
    mc.lineTo(140, BUTTON_H);
    mc.lineTo(0, BUTTON_H);
    mc.endFill();
    mc.lineStyle(1, hover ? 0xFFFFBB : COLOR_GOLD, hover ? 90 : 70);
    mc.moveTo(0, 0);
    mc.lineTo(140, 0);
    mc.lineTo(140, BUTTON_H);
    mc.lineTo(0, BUTTON_H);
    mc.lineTo(0, 0);
    if (mc["lbl"] == undefined) {
        mc.createTextField("lbl", 0, 0, 6, 140, 22);
        mc["lbl"].selectable = false;
        _applyFmt(mc["lbl"], _makeFmt(FONT_SIZE + 1, true, COLOR_BRIGHT, "center"));
    }
    mc["lbl"].text = "Confirm";
}

// ====================================================================
// Reset button
// ====================================================================

function _makeResetBtn(mc) {
    _drawReset(mc, false);
    mc.useHandCursor = true;
    mc.onRollOver = function() { _drawReset(this, true); };
    mc.onRollOut  = function() { _drawReset(this, false); };
    mc.onRelease  = function() {
        gfx.io.GameDelegate.call("EA_OnReset", []);
    };
}

function _drawReset(mc, hover) {
    mc.clear();
    mc.beginFill(0x333333, hover ? 75 : 55);
    mc.moveTo(0, 0);
    mc.lineTo(140, 0);
    mc.lineTo(140, BUTTON_H);
    mc.lineTo(0, BUTTON_H);
    mc.endFill();
    mc.lineStyle(1, hover ? 0xFFFFBB : COLOR_GOLD, hover ? 90 : 70);
    mc.moveTo(0, 0);
    mc.lineTo(140, 0);
    mc.lineTo(140, BUTTON_H);
    mc.lineTo(0, BUTTON_H);
    mc.lineTo(0, 0);
    if (mc["lbl"] == undefined) {
        mc.createTextField("lbl", 0, 0, 6, 140, 22);
        mc["lbl"].selectable = false;
        _applyFmt(mc["lbl"], _makeFmt(FONT_SIZE + 1, true, COLOR_BRIGHT, "center"));
    }
    mc["lbl"].text = "Reset";
}
