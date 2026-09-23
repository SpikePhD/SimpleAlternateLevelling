// Simple Alternate Levelling - transactional skill allocation UI (AS2)
//
// Skyrim-style layout: dimmed full-screen backdrop, centred header, three
// labelled skill groups with -/+ per skill, and text-style footer buttons.
// All values are previews; the plugin validates and commits on Confirm.

Stage.scaleMode = "showAll";
Stage.align = "";

var g_skillData = [];
var g_remainingPoints = 0;
var g_totalPoints = 0;
var g_carryOver = 0;
var g_skillCap = 200;
var g_closing = false;
var g_selected = 0;
var g_lastColumn = 0;
var g_rows = [];          // row movie clips, index = skill index
var g_footer = [];        // [reset, confirm]
var g_labels = {};
var g_pointsTF = undefined;

var g_pointsLabel = "Distribute Skill Points";
var g_confirmLabel = "Confirm";
var g_resetLabel = "Reset";

// Layout on the 1280x720 stage.
var STAGE_W = 1280;
var STAGE_H = 720;
var COL_W = 300;
var COL_X = [160, 490, 820];
var HEADING_Y = 206;
var ROW_START_Y = 244;
var ROW_H = 42;
var FOOTER_Y = 532;
var FOOTER_BTN_W = 170;
var FOOTER_BTN_H = 40;

var RESET_INDEX = 18;
var CONFIRM_INDEX = 19;

var COLOR_TITLE = 0xEDE3C4;
var COLOR_GOLD = 0xC8B878;
var COLOR_GOLD_BRIGHT = 0xF0CC5A;
var COLOR_TEXT = 0xD8D2BE;
var COLOR_WHITE = 0xFFFFFF;
var COLOR_MUTED = 0x9A937E;
var COLOR_DISABLED = 0x5E5A50;

// ---------------------------------------------------------------------------
// Plugin -> menu interface
// ---------------------------------------------------------------------------

// Argument order matches InvokeInit in src/SkillMenu.cpp.
function EA_Init(skillData, totalPoints, carryOver, pointsLabel, confirmLabel, resetLabel, info) {
    g_skillData = skillData;
    g_remainingPoints = totalPoints;
    g_totalPoints = totalPoints;
    g_carryOver = (carryOver != undefined) ? carryOver : 0;
    g_closing = false;
    if (g_skillData.length > 0 && g_skillData[0].skillCap != undefined) {
        g_skillCap = g_skillData[0].skillCap;
    }
    for (var i = 0; i < g_skillData.length; i++) {
        g_skillData[i].originalLevel = g_skillData[i].currentLevel;
        g_skillData[i].delta = 0;
    }
    if (pointsLabel != undefined && pointsLabel.length > 0) { g_pointsLabel = pointsLabel; }
    if (confirmLabel != undefined && confirmLabel.length > 0) { g_confirmLabel = confirmLabel; }
    if (resetLabel != undefined && resetLabel.length > 0) { g_resetLabel = resetLabel; }

    g_labels = {
        level: 0, levelLabel: "Level", remainingLabel: "points remaining",
        carriedLabel: "carried over from earlier levels", maxLabel: "Max",
        combatLabel: "Combat", magicLabel: "Magic", stealthLabel: "Stealth", hint: ""
    };
    if (info != undefined) {
        for (var key in g_labels) {
            if (info[key] != undefined) { g_labels[key] = info[key]; }
        }
    }

    _build();
    _select(_firstEnabledRow());
}

function EA_UpdateSkill(actorValue, newLevel, delta) {
    for (var i = 0; i < g_skillData.length; i++) {
        var sk = g_skillData[i];
        if (sk.actorValue == actorValue) {
            sk.currentLevel = newLevel;
            sk.delta = (delta != undefined) ? delta : Math.max(0, Math.round(newLevel - sk.originalLevel));
            _drawRow(i);
            break;
        }
    }
    _refreshAll();
}

function EA_UpdatePoints(remaining) {
    g_remainingPoints = remaining;
    _refreshAll();
}

function EA_SetClosing() {
    g_closing = true;
    _refreshAll();
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

function _fmt(size, color, align, spacing) {
    var fmt = new TextFormat();
    fmt.font = "$EverywhereMediumFont";
    fmt.size = size;
    fmt.color = color;
    fmt.align = (align != undefined) ? align : "left";
    if (spacing != undefined) { fmt.letterSpacing = spacing; }
    return fmt;
}

function _text(parent, name, depth, x, y, w, h, value, fmt) {
    parent.createTextField(name, depth, x, y, w, h);
    var tf = parent[name];
    tf.selectable = false;
    tf.setNewTextFormat(fmt);
    tf.text = value;
    tf.setTextFormat(fmt);
    return tf;
}

function _setText(tf, value, fmt) {
    tf.text = value;
    tf.setNewTextFormat(fmt);
    tf.setTextFormat(fmt);
}

function _rect(mc, x, y, w, h, color, alpha) {
    mc.beginFill(color, alpha);
    mc.moveTo(x, y); mc.lineTo(x + w, y); mc.lineTo(x + w, y + h); mc.lineTo(x, y + h); mc.lineTo(x, y);
    mc.endFill();
}

// Horizontal bar that fades out at both ends.
function _fadeBar(mc, x, y, w, h, color, peakAlpha) {
    var matrix = { matrixType: "box", x: x, y: y, w: w, h: h, r: 0 };
    mc.beginGradientFill("linear", [color, color, color], [0, peakAlpha, 0], [0, 127, 255], matrix);
    mc.moveTo(x, y); mc.lineTo(x + w, y); mc.lineTo(x + w, y + h); mc.lineTo(x, y + h); mc.lineTo(x, y);
    mc.endFill();
}

// Vertical fade from alphaTop to alphaBottom.
function _verticalFade(mc, x, y, w, h, color, alphaTop, alphaBottom) {
    var matrix = { matrixType: "box", x: x, y: y, w: w, h: h, r: Math.PI / 2 };
    mc.beginGradientFill("linear", [color, color], [alphaTop, alphaBottom], [0, 255], matrix);
    mc.moveTo(x, y); mc.lineTo(x + w, y); mc.lineTo(x + w, y + h); mc.lineTo(x, y + h); mc.lineTo(x, y);
    mc.endFill();
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

function _build() {
    _root.menuMC.removeMovieClip();
    var m = _root.createEmptyMovieClip("menuMC", 10);
    g_rows = [];
    g_footer = [];

    // Backdrop: hide the stats screen behind (its skill strip and attribute
    // chooser otherwise show through the rows), darker at top and bottom.
    var bg = m.createEmptyMovieClip("backdrop", 1);
    _rect(bg, 0, 0, STAGE_W, STAGE_H, 0x000000, 86);
    _verticalFade(bg, 0, 0, STAGE_W, 150, 0x000000, 70, 0);
    _verticalFade(bg, 0, STAGE_H - 150, STAGE_W, 150, 0x000000, 0, 70);
    // Darker band behind the skill grid, fading out towards the sides.
    _fadeBar(bg, 0, HEADING_Y - 16, STAGE_W, FOOTER_Y - HEADING_Y - 4, 0x000000, 75);
    bg.useHandCursor = false;
    bg.onRelease = function() {};  // swallow clicks meant for the menu behind

    var level = Number(g_labels.level);
    var levelText = (level > 0) ? (g_labels.levelLabel + " " + level).toUpperCase() : "";
    _text(m, "levelTF", 10, 0, 30, STAGE_W, 22, levelText, _fmt(13, COLOR_MUTED, "center", 4));
    _text(m, "titleTF", 11, 0, 50, STAGE_W, 38, g_pointsLabel, _fmt(26, COLOR_TITLE, "center"));
    g_pointsTF = _text(m, "pointsTF", 12, 0, 90, STAGE_W, 56, "", _fmt(42, COLOR_WHITE, "center"));
    _text(m, "remainingTF", 13, 0, 144, STAGE_W, 22, g_labels.remainingLabel, _fmt(14, COLOR_MUTED, "center"));
    if (g_carryOver > 0) {
        _text(m, "carryTF", 14, 0, 164, STAGE_W, 20, "+" + g_carryOver + " " + g_labels.carriedLabel, _fmt(12, COLOR_GOLD, "center"));
    }

    var lines = m.createEmptyMovieClip("lines", 20);
    _fadeBar(lines, 160, 192, 960, 1, COLOR_GOLD, 70);
    _fadeBar(lines, 160, FOOTER_Y - 18, 960, 1, COLOR_GOLD, 70);

    var groups = [g_labels.combatLabel, g_labels.magicLabel, g_labels.stealthLabel];
    for (var c = 0; c < 3; c++) {
        _text(m, "group" + c, 30 + c, COL_X[c], HEADING_Y, COL_W, 24, String(groups[c]).toUpperCase(), _fmt(14, COLOR_GOLD, "center", 4));
        _fadeBar(lines, COL_X[c] + 20, HEADING_Y + 28, COL_W - 40, 1, COLOR_GOLD, 55);
    }

    var depth = 100;
    for (var i = 0; i < g_skillData.length; i++) {
        var sk = g_skillData[i];
        var row = m.createEmptyMovieClip("row" + i, depth++);
        row._x = COL_X[sk.column];
        row._y = ROW_START_Y + sk.row * ROW_H;
        row.skillIndex = i;
        _buildRow(row, sk);
        g_rows[i] = row;
    }

    var footerX = Math.floor(STAGE_W / 2);
    var reset = m.createEmptyMovieClip("resetMC", depth++);
    reset._x = footerX - FOOTER_BTN_W - 20; reset._y = FOOTER_Y; reset.kind = "reset"; reset.label = g_resetLabel;
    var confirm = m.createEmptyMovieClip("confirmMC", depth++);
    confirm._x = footerX + 20; confirm._y = FOOTER_Y; confirm.kind = "confirm"; confirm.label = g_confirmLabel;
    g_footer = [reset, confirm];
    for (var f = 0; f < 2; f++) {
        var btn = g_footer[f];
        btn.index = RESET_INDEX + f;
        btn.createEmptyMovieClip("bg", 1);
        _text(btn, "lbl", 2, 0, 9, FOOTER_BTN_W, 24, btn.label, _fmt(17, COLOR_TEXT, "center"));
        btn.onRollOver = function() { _select(this.index); };
        btn.onRelease = function() { _activate(this.index); };
    }

    if (g_labels.hint != undefined && g_labels.hint.length > 0) {
        _text(m, "hintTF", depth++, 0, FOOTER_Y + 62, STAGE_W, 20, g_labels.hint, _fmt(12, COLOR_MUTED, "center"));
    }
    _refreshAll();
}

function _buildRow(row, sk) {
    row.createEmptyMovieClip("highlight", 1);

    // Hover target for the whole row; sits below the -/+ buttons.
    var hit = row.createEmptyMovieClip("hit", 2);
    _rect(hit, -10, 0, COL_W + 20, ROW_H - 4, 0x000000, 0);
    hit.skillIndex = row.skillIndex;
    hit.useHandCursor = false;
    hit.onRollOver = function() { _select(this.skillIndex); };
    hit.onRelease = function() { _select(this.skillIndex); };

    _text(row, "nameTF", 3, 8, 8, 150, 26, sk.name, _fmt(16, COLOR_TEXT));
    _text(row, "deltaTF", 4, 136, 11, 52, 22, "", _fmt(13, COLOR_GOLD_BRIGHT, "right"));
    _text(row, "valueTF", 5, 226, 6, 48, 30, "", _fmt(18, COLOR_WHITE, "center"));

    var minus = row.createEmptyMovieClip("minusMC", 6);
    minus._x = 214; minus._y = 19; minus.sign = -1;
    var plus = row.createEmptyMovieClip("plusMC", 7);
    plus._x = 286; plus._y = 19; plus.sign = 1;
    var buttons = [minus, plus];
    for (var b = 0; b < 2; b++) {
        var btn = buttons[b];
        btn.skillIndex = row.skillIndex;
        btn.onRollOver = function() { _select(this.skillIndex); };
        btn.onRelease = function() {
            _select(this.skillIndex);
            if (this.sign > 0) { _allocate(this.skillIndex); } else { _deallocate(this.skillIndex); }
        };
    }
    _drawRow(row.skillIndex);
}

// ---------------------------------------------------------------------------
// State and rendering
// ---------------------------------------------------------------------------

function _canAdd(i) {
    return !g_closing && g_remainingPoints > 0 && g_skillData[i].currentLevel < g_skillCap;
}

function _canRemove(i) {
    return !g_closing && g_skillData[i].delta > 0;
}

function _hasChanges() {
    for (var i = 0; i < g_skillData.length; i++) {
        if (g_skillData[i].delta > 0) { return true; }
    }
    return false;
}

function _footerDisabled(index) {
    if (g_closing) { return true; }
    return index == RESET_INDEX && !_hasChanges();
}

function _drawSignButton(btn, enabled, emphasised) {
    btn.clear();
    // Transparent disc keeps the whole button clickable.
    btn.beginFill(0x000000, 0);
    btn.moveTo(-14, -14); btn.lineTo(14, -14); btn.lineTo(14, 14); btn.lineTo(-14, 14); btn.lineTo(-14, -14);
    btn.endFill();
    var color = enabled ? (emphasised ? COLOR_WHITE : COLOR_GOLD) : COLOR_DISABLED;
    var alpha = enabled ? 100 : 60;
    btn.lineStyle(1, color, enabled ? 70 : 40);
    var r = 11;
    // Octagon approximates a circle without relying on curveTo support.
    for (var k = 0; k <= 8; k++) {
        var angle = k * Math.PI / 4 + Math.PI / 8;
        var px = Math.cos(angle) * r; var py = Math.sin(angle) * r;
        if (k == 0) { btn.moveTo(px, py); } else { btn.lineTo(px, py); }
    }
    btn.lineStyle(2, color, alpha);
    btn.moveTo(-5, 0); btn.lineTo(5, 0);
    if (btn.sign > 0) { btn.moveTo(0, -5); btn.lineTo(0, 5); }
    btn.useHandCursor = enabled;
    btn.enabled = true;
}

function _drawRow(i) {
    var row = g_rows[i];
    if (row == undefined) { return; }
    var sk = g_skillData[i];
    var selected = (i == g_selected);
    var atCap = sk.currentLevel >= g_skillCap;

    row.highlight.clear();
    if (selected && !g_closing) {
        _fadeBar(row.highlight, -10, 1, COL_W + 20, ROW_H - 6, COLOR_GOLD, 22);
        _fadeBar(row.highlight, -10, 0, COL_W + 20, 1, COLOR_GOLD, 70);
        _fadeBar(row.highlight, -10, ROW_H - 5, COL_W + 20, 1, COLOR_GOLD, 70);
    }

    _setText(row.nameTF, sk.name, _fmt(16, selected ? COLOR_WHITE : COLOR_TEXT));
    var valueColor = atCap ? COLOR_GOLD : (sk.delta > 0 ? COLOR_GOLD_BRIGHT : COLOR_WHITE);
    _setText(row.valueTF, String(Math.floor(sk.currentLevel)), _fmt(18, valueColor, "center"));
    var deltaText = "";
    if (sk.delta > 0) { deltaText = "+" + sk.delta; }
    else if (atCap) { deltaText = g_labels.maxLabel; }
    _setText(row.deltaTF, deltaText, _fmt(13, sk.delta > 0 ? COLOR_GOLD_BRIGHT : COLOR_MUTED, "right"));

    _drawSignButton(row.minusMC, _canRemove(i), selected);
    _drawSignButton(row.plusMC, _canAdd(i), selected);
}

function _drawFooter(index) {
    var btn = g_footer[index - RESET_INDEX];
    var selected = (index == g_selected);
    var disabled = _footerDisabled(index);
    btn.bg.clear();
    if (selected && !disabled) {
        _fadeBar(btn.bg, -30, 0, FOOTER_BTN_W + 60, FOOTER_BTN_H, COLOR_GOLD, 30);
        _fadeBar(btn.bg, 0, FOOTER_BTN_H - 1, FOOTER_BTN_W, 2, COLOR_GOLD_BRIGHT, 100);
    } else {
        _fadeBar(btn.bg, 20, FOOTER_BTN_H - 1, FOOTER_BTN_W - 40, 1, COLOR_GOLD, disabled ? 25 : 60);
    }
    // Transparent fill keeps the button clickable across its full area.
    _rect(btn.bg, 0, 0, FOOTER_BTN_W, FOOTER_BTN_H, 0x000000, 0);
    var color = disabled ? COLOR_DISABLED : (selected ? COLOR_WHITE : (btn.kind == "confirm" ? COLOR_GOLD_BRIGHT : COLOR_TEXT));
    _setText(btn.lbl, btn.label, _fmt(17, color, "center"));
    btn.useHandCursor = !disabled;
}

function _refreshAll() {
    if (g_pointsTF != undefined) {
        _setText(g_pointsTF, String(g_remainingPoints), _fmt(42, g_remainingPoints > 0 ? COLOR_WHITE : COLOR_MUTED, "center"));
    }
    for (var i = 0; i < g_rows.length; i++) { _drawRow(i); }
    if (g_footer.length == 2) {
        _drawFooter(RESET_INDEX);
        _drawFooter(CONFIRM_INDEX);
    }
}

// ---------------------------------------------------------------------------
// Actions (menu -> plugin)
// ---------------------------------------------------------------------------

function _allocate(i) {
    if (!_canAdd(i)) { return; }
    gfx.io.GameDelegate.call("EA_OnAllocate", [g_skillData[i].actorValue]);
}

function _deallocate(i) {
    if (!_canRemove(i)) { return; }
    gfx.io.GameDelegate.call("EA_OnDeallocate", [g_skillData[i].actorValue]);
}

function _activate(index) {
    if (g_closing) { return; }
    if (index < RESET_INDEX) { _allocate(index); return; }
    if (_footerDisabled(index)) { return; }
    if (index == RESET_INDEX) {
        gfx.io.GameDelegate.call("EA_OnReset", []);
    } else if (index == CONFIRM_INDEX) {
        g_closing = true;
        _refreshAll();
        gfx.io.GameDelegate.call("EA_OnConfirm", []);
    }
}

// ---------------------------------------------------------------------------
// Selection and keyboard navigation
// ---------------------------------------------------------------------------

function _select(index) {
    if (index < 0 || index > CONFIRM_INDEX) { return; }
    var previous = g_selected;
    g_selected = index;
    if (index < RESET_INDEX) { g_lastColumn = g_skillData[index].column; }
    if (previous < RESET_INDEX) { _drawRow(previous); } else if (g_footer.length == 2) { _drawFooter(previous); }
    if (index < RESET_INDEX) { _drawRow(index); } else if (g_footer.length == 2) { _drawFooter(index); }
}

function _firstEnabledRow() {
    return 0;
}

function _findRow(column, row) {
    for (var i = 0; i < g_skillData.length; i++) {
        if (g_skillData[i].column == column && g_skillData[i].row == row) { return i; }
    }
    return -1;
}

function _navigate(code) {
    if (g_selected >= RESET_INDEX) {
        if (code == Key.LEFT || code == Key.RIGHT) { _select(g_selected == RESET_INDEX ? CONFIRM_INDEX : RESET_INDEX); }
        else if (code == Key.UP) { _select(_findRow(g_lastColumn, 5)); }
        return;
    }
    var sk = g_skillData[g_selected];
    var column = sk.column;
    var row = sk.row;
    if (code == Key.LEFT) { column = Math.max(0, column - 1); }
    if (code == Key.RIGHT) { column = Math.min(2, column + 1); }
    if (code == Key.UP) { row = Math.max(0, row - 1); }
    if (code == Key.DOWN) {
        if (row == 5) { _select(column < 2 ? RESET_INDEX : CONFIRM_INDEX); return; }
        row++;
    }
    var next = _findRow(column, row);
    if (next >= 0) { _select(next); }
}

var g_keyListener = {};
g_keyListener.onKeyDown = function() {
    if (g_closing || g_rows.length == 0) { return; }
    var code = Key.getCode();
    if (code == Key.ESCAPE || code == 27 || code == 67) { _activate(CONFIRM_INDEX); return; }
    if (code == 82) { _activate(RESET_INDEX); return; }
    if (code == Key.ENTER || code == 13 || code == 32 || code == 187 || code == 107 || code == 61) {
        _activate(g_selected);
        return;
    }
    if (code == Key.BACKSPACE || code == 8 || code == Key.DELETEKEY || code == 46 ||
        code == 189 || code == 109 || code == 173) {
        if (g_selected < RESET_INDEX) { _deallocate(g_selected); }
        return;
    }
    if (code == Key.TAB || code == 9) {
        var direction = Key.isDown(Key.SHIFT) ? -1 : 1;
        _select((g_selected + direction + CONFIRM_INDEX + 1) % (CONFIRM_INDEX + 1));
        return;
    }
    if (code == Key.LEFT || code == Key.RIGHT || code == Key.UP || code == Key.DOWN) { _navigate(code); }
};
Key.addListener(g_keyListener);
