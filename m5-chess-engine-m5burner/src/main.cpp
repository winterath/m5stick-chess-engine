#include <Arduino.h>
#include <M5Unified.h>

#include <cstring>

#include "ChessEngine.h"

struct Preset {
  const char* name;
  const char* fen;
};

static constexpr uint16_t COLOR_BG = 0x0841;
static constexpr uint16_t COLOR_TEXT = 0xFFFF;
static constexpr uint16_t COLOR_MUTED = 0xBDF7;
static constexpr uint16_t COLOR_ACCENT = 0x07FF;
static constexpr uint16_t COLOR_WARN = 0xFD20;

static Preset presets[] = {
    {"Start", ChessEngine::START_FEN},
    {"Kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/2P5/1p2P3/2N2N2/PP1PBPPP/R2QKB1R w KQkq - 0 1"},
    {"Attack", "r1bq1rk1/ppp2ppp/2n2n2/3pp3/3PP3/2P2N2/PP1N1PPP/R1BQKB1R w KQ - 4 7"},
    {"Endgame", "8/5pk1/4p1p1/3pP3/3P1P2/6K1/8/8 w - - 0 42"},
    {"Mate net", "6k1/5ppp/8/8/8/8/5PPP/5RK1 w - - 0 1"},
};

static ChessEngine engine;
static ChessSearchResult lastSearch;
static String serialLine;
static String statusText = "Ready";
static String presetName = "Start";
static int searchDepth = 3;
static uint8_t presetIndex = 0;

static String toDeviceString(const std::string& text) {
  return String(text.c_str());
}

static void serialPrintHelp() {
  Serial.println();
  Serial.println("M5 Chess Engine commands:");
  Serial.println("  help");
  Serial.println("  new");
  Serial.println("  fen <full FEN>");
  Serial.println("  go depth 3");
  Serial.println("  depth 4");
  Serial.println("  move e2e4");
  Serial.println("  moves");
  Serial.println("  state");
  Serial.println();
}

static void drawWrapped(const String& text, int x, int& y, int width, int lineHeight) {
  int charWidth = 6;
  int maxChars = max(8, width / charWidth);
  int start = 0;
  while (start < text.length() && y < M5.Display.height() - 28) {
    int count = min(maxChars, text.length() - start);
    M5.Display.setCursor(x, y);
    M5.Display.print(text.substring(start, start + count));
    y += lineHeight;
    start += count;
  }
}

static void drawScreen() {
  int w = M5.Display.width();
  int h = M5.Display.height();
  M5.Display.fillScreen(COLOR_BG);
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextWrap(false);

  int y = 8;
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(COLOR_ACCENT, COLOR_BG);
  M5.Display.setCursor(8, y);
  M5.Display.print("M5 Chess Engine");

  y += 28;
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(COLOR_TEXT, COLOR_BG);
  M5.Display.setCursor(8, y);
  M5.Display.printf("Preset: %s", presetName.c_str());

  y += 16;
  M5.Display.setCursor(8, y);
  M5.Display.printf("Side: %s   Depth: %d", engine.whiteToMove() ? "White" : "Black", searchDepth);
  if (engine.inCheck()) {
    M5.Display.setTextColor(COLOR_WARN, COLOR_BG);
    M5.Display.print("   Check");
    M5.Display.setTextColor(COLOR_TEXT, COLOR_BG);
  }

  y += 18;
  M5.Display.setTextColor(COLOR_MUTED, COLOR_BG);
  M5.Display.setCursor(8, y);
  M5.Display.print("FEN");
  y += 13;
  M5.Display.setTextColor(COLOR_TEXT, COLOR_BG);
  drawWrapped(toDeviceString(engine.toFen()), 8, y, w - 16, 12);

  y += 4;
  M5.Display.setTextColor(COLOR_MUTED, COLOR_BG);
  M5.Display.setCursor(8, y);
  M5.Display.print("Result");
  y += 14;
  M5.Display.setTextColor(COLOR_TEXT, COLOR_BG);
  M5.Display.setCursor(8, y);
  if (lastSearch.hasMove) {
    M5.Display.printf("Best %s  Score %+d", engine.moveToUci(lastSearch.bestMove).c_str(), lastSearch.score);
    y += 14;
    M5.Display.setCursor(8, y);
    M5.Display.printf("Nodes %lu", static_cast<unsigned long>(lastSearch.nodes));
  } else if (lastSearch.checkmate) {
    M5.Display.print("Checkmate");
  } else if (lastSearch.stalemate) {
    M5.Display.print("Stalemate");
  } else {
    M5.Display.print(statusText);
  }

  M5.Display.fillRect(0, h - 24, w, 24, 0x2104);
  M5.Display.setTextColor(COLOR_TEXT, 0x2104);
  M5.Display.setCursor(8, h - 17);
  M5.Display.print("A Preset");
  M5.Display.setCursor(w / 2 - 34, h - 17);
  M5.Display.print("B Search");
  M5.Display.setCursor(w - 70, h - 17);
  M5.Display.print("C Depth");
}

static void resetToPreset(uint8_t index) {
  presetIndex = index % (sizeof(presets) / sizeof(presets[0]));
  std::string error;
  engine.setFromFen(presets[presetIndex].fen, &error);
  presetName = presets[presetIndex].name;
  statusText = "Ready";
  lastSearch = ChessSearchResult();
  Serial.printf("Loaded preset: %s\n", presets[presetIndex].name);
  Serial.printf("FEN: %s\n", engine.toFen().c_str());
  drawScreen();
}

static void runSearch(int depth) {
  searchDepth = constrain(depth, 1, 5);
  statusText = "Searching...";
  lastSearch = ChessSearchResult();
  drawScreen();

  uint32_t start = millis();
  lastSearch = engine.search(searchDepth);
  uint32_t elapsed = millis() - start;

  if (lastSearch.hasMove) {
    statusText = "Best " + toDeviceString(engine.moveToUci(lastSearch.bestMove));
    Serial.printf(
        "bestmove %s score %+d depth %d nodes %lu time_ms %lu\n",
        engine.moveToUci(lastSearch.bestMove).c_str(),
        lastSearch.score,
        searchDepth,
        static_cast<unsigned long>(lastSearch.nodes),
        static_cast<unsigned long>(elapsed));
  } else if (lastSearch.checkmate) {
    statusText = "Checkmate";
    Serial.println("checkmate");
  } else {
    statusText = "Stalemate";
    Serial.println("stalemate");
  }
  drawScreen();
}

static String afterCommand(const String& line, const char* command) {
  String rest = line.substring(strlen(command));
  rest.trim();
  return rest;
}

static void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) {
    return;
  }

  String lower = line;
  lower.toLowerCase();

  if (lower == "help" || lower == "?") {
    serialPrintHelp();
    return;
  }

  if (lower == "new") {
    resetToPreset(0);
    return;
  }

  if (lower == "state") {
    Serial.printf("fen %s\n", engine.toFen().c_str());
    Serial.printf("side %s\n", engine.whiteToMove() ? "white" : "black");
    Serial.printf("depth %d\n", searchDepth);
    return;
  }

  if (lower.startsWith("depth ")) {
    int nextDepth = afterCommand(line, "depth").toInt();
    searchDepth = constrain(nextDepth, 1, 5);
    statusText = "Depth " + String(searchDepth);
    lastSearch = ChessSearchResult();
    Serial.printf("depth %d\n", searchDepth);
    drawScreen();
    return;
  }

  if (lower.startsWith("fen ")) {
    String fenText = afterCommand(line, "fen");
    std::string error;
    if (engine.setFromFen(fenText.c_str(), &error)) {
      presetName = "USB FEN";
      statusText = "Ready";
      lastSearch = ChessSearchResult();
      Serial.printf("ok fen %s\n", engine.toFen().c_str());
    } else {
      statusText = "Bad FEN";
      Serial.printf("error %s\n", error.c_str());
    }
    drawScreen();
    return;
  }

  if (lower.startsWith("move ")) {
    String moveText = afterCommand(line, "move");
    std::string error;
    if (engine.makeMoveUci(moveText.c_str(), &error)) {
      statusText = "Moved " + moveText;
      lastSearch = ChessSearchResult();
      Serial.printf("ok move %s\n", moveText.c_str());
      Serial.printf("fen %s\n", engine.toFen().c_str());
    } else {
      statusText = "Illegal move";
      Serial.printf("error %s\n", error.c_str());
    }
    drawScreen();
    return;
  }

  if (lower == "moves") {
    std::vector<ChessMove> moves = engine.legalMoves();
    Serial.printf("moves %u\n", static_cast<unsigned>(moves.size()));
    for (const ChessMove& move : moves) {
      Serial.printf("%s ", engine.moveToUci(move).c_str());
    }
    Serial.println();
    return;
  }

  if (lower.startsWith("go")) {
    int depth = searchDepth;
    int marker = lower.indexOf("depth");
    if (marker >= 0) {
      String depthText = lower.substring(marker + 5);
      depthText.trim();
      depth = depthText.toInt();
    }
    runSearch(depth);
    return;
  }

  Serial.println("error unknown command; type help");
}

static void readSerialCommands() {
  while (Serial.available()) {
    char ch = static_cast<char>(Serial.read());
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      handleCommand(serialLine);
      serialLine = "";
    } else if (serialLine.length() < 240) {
      serialLine += ch;
    }
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(200);

  M5.Display.setRotation(1);
  resetToPreset(0);
  serialPrintHelp();
}

void loop() {
  M5.update();
  readSerialCommands();

  if (M5.BtnA.wasPressed()) {
    resetToPreset(presetIndex + 1);
  }
  if (M5.BtnB.wasPressed()) {
    runSearch(searchDepth);
  }
  if (M5.BtnC.wasPressed()) {
    searchDepth = searchDepth >= 5 ? 1 : searchDepth + 1;
    statusText = "Depth " + String(searchDepth);
    lastSearch = ChessSearchResult();
    drawScreen();
  }
}
