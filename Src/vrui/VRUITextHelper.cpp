#include "pch.h"
#include "VRUITextHelper.h"
#include "VRUIWidget.h"
#include <sstream>
#include <cctype>
#include <algorithm>
#include <unordered_map>
#include <RE/Skyrim.h>

namespace vrui
{
    namespace
    {
        std::string getUtf8FallbackNifName(const std::string& utf8Name)
        {
            static const std::unordered_map<std::string, std::string> kFallbackMap = {
                { "utf8_195_128", "A" }, // À
                { "utf8_195_130", "A" }, // Â
                { "utf8_195_132", "A" }, // Ä
                { "utf8_195_136", "E" }, // È
                { "utf8_195_138", "E" }, // Ê
                { "utf8_195_139", "E" }, // Ë
                { "utf8_195_140", "I" }, // Ì
                { "utf8_195_142", "I" }, // Î
                { "utf8_195_143", "I" }, // Ï
                { "utf8_195_146", "O" }, // Ò
                { "utf8_195_148", "O" }, // Ô
                { "utf8_195_150", "O" }, // Ö
                { "utf8_195_153", "U" }, // Ù
                { "utf8_195_155", "U" }, // Û
                { "utf8_195_156", "U" }, // Ü
                { "utf8_195_160", "A" }, // à
                { "utf8_195_162", "A" }, // â
                { "utf8_195_164", "A" }, // ä
                { "utf8_195_168", "E" }, // è
                { "utf8_195_170", "E" }, // ê
                { "utf8_195_171", "E" }, // ë
                { "utf8_195_172", "I" }, // ì
                { "utf8_195_174", "I" }, // î
                { "utf8_195_175", "I" }, // ï
                { "utf8_195_178", "O" }, // ò
                { "utf8_195_180", "O" }, // ô
                { "utf8_195_182", "O" }, // ö
                { "utf8_195_185", "U" }, // ù
                { "utf8_195_187", "U" }, // û
                { "utf8_195_188", "U" }  // ü
            };

            auto it = kFallbackMap.find(utf8Name);
            return it != kFallbackMap.end() ? it->second : "";
        }
    }

    std::vector<std::string> VRUITextHelper::wrapText(const std::string& text, int maxChars)
    {
        std::vector<std::string> lines;
        if (text.empty()) return lines;

        std::istringstream stream(text);
        std::string word;
        std::string currentLine;

        while (stream >> word) {
            if (currentLine.empty()) {
                currentLine = word;
            } else if (static_cast<int>(currentLine.size()) + 1 + static_cast<int>(word.size()) <= maxChars) {
                currentLine += ' ';
                currentLine += word;
            } else {
                lines.push_back(currentLine);
                currentLine = word;
            }
        }
        if (!currentLine.empty()) lines.push_back(currentLine);
        return lines;
    }

    float VRUITextHelper::buildTextLine(RE::NiNode* parentNode, const std::string& line,
                               float charScale, float spacing, float lineY,
                               std::vector<RE::NiPointer<RE::NiAVObject>>& outNodes,
                               size_t& poolIndex)
    {
        float currentX = 0.0f;

        for (size_t i = 0; i < line.length(); ) {
            unsigned char c = static_cast<unsigned char>(line[i]);
            
            if (c == ' ' || c == '\t') {
                currentX += spacing * 2.0f;
                i++;
                continue;
            }

            std::string nifName;
            size_t bytesConsumed = 1;

            if (c < 0x80) { // ASCII
                switch (c) {
                    case '<': nifName = "Less"; break;
                    case '>': nifName = "Greater"; break;
                    case ':': nifName = "Colon"; break;
                    case '-': nifName = "Minus"; break;
                    case '+': nifName = "Plus"; break;
                    case '.': nifName = "Dot"; break;
                    case ',': nifName = "Comma"; break;
                    case '/': nifName = "Slash"; break;
                    case '\\': nifName = "Backslash"; break;
                    case '*': nifName = "symbol"; break;
                    case '?': nifName = "Question"; break;
                    case '"': nifName = "Quote"; break;
                    case '|': nifName = "Pipe"; break;
                    case '=': nifName = "Equals"; break;
                    case '_': nifName = "Underscore"; break;
                    case '!': nifName = "Exclamation"; break;
                    case '@': nifName = "At"; break;
                    case '#': nifName = "Hash"; break;
                    case '$': nifName = "Dollar"; break;
                    case '%': nifName = "Percent"; break;
                    case '&': nifName = "Ampersand"; break;
                    case '(': nifName = "ParenLeft"; break;
                    case ')': nifName = "ParenRight"; break;
                    case '[': nifName = "BracketLeft"; break;
                    case ']': nifName = "BracketRight"; break;
                    case '{': nifName = "BraceLeft"; break;
                    case '}': nifName = "BraceRight"; break;
                    case '\'': nifName = "Apostrophe"; break;
                    default:
                        if (c < 32) nifName = std::to_string(c);
                        else nifName = std::string(1, static_cast<char>(toupper(c)));
                        break;
                }
                bytesConsumed = 1;
            } else if ((c & 0xE0) == 0xC0) {
                if (i + 1 < line.length()) {
                    unsigned char c2 = static_cast<unsigned char>(line[i+1]);
                    nifName = "utf8_" + std::to_string(c) + "_" + std::to_string(c2);
                    bytesConsumed = 2;
                } else { nifName = std::to_string(c); bytesConsumed = 1; }
            } else if ((c & 0xF0) == 0xE0) {
                if (i + 2 < line.length()) {
                    unsigned char c2 = static_cast<unsigned char>(line[i+1]);
                    unsigned char c3 = static_cast<unsigned char>(line[i+2]);
                    nifName = "utf8_" + std::to_string(c) + "_" + std::to_string(c2) + "_" + std::to_string(c3);
                    bytesConsumed = 3;
                } else { nifName = std::to_string(c); bytesConsumed = 1; }
            } else { nifName = std::to_string(c); bytesConsumed = 1; }

            std::string nifPath = "DragonBoardVR\\font\\" + nifName + ".nif";
            RE::NiPointer<RE::NiAVObject> charModel;

            // --- OPTIMIZATION: Node Pooling ---
            if (poolIndex < outNodes.size()) {
                auto& existing = outNodes[poolIndex];
                if (existing && existing->name.c_str() == nifName) {
                    charModel = existing;
                    charModel->SetAppCulled(false);
                } else {
                    if (existing) parentNode->DetachChild(existing.get());
                    charModel = VRUIWidget::loadModelFromNif(nifPath);
                    if (!charModel) {
                        std::string fallbackName = getUtf8FallbackNifName(nifName);
                        if (!fallbackName.empty()) {
                            charModel = VRUIWidget::loadModelFromNif("DragonBoardVR\\font\\" + fallbackName + ".nif");
                            if (charModel) {
                                nifName = fallbackName;
                            }
                        }
                    }
                    if (charModel) {
                        charModel->name = nifName;
                        parentNode->AttachChild(charModel.get());
                        outNodes[poolIndex] = charModel;
                    }
                }
            } else {
                charModel = VRUIWidget::loadModelFromNif(nifPath);
                if (!charModel) {
                    std::string fallbackName = getUtf8FallbackNifName(nifName);
                    if (!fallbackName.empty()) {
                        charModel = VRUIWidget::loadModelFromNif("DragonBoardVR\\font\\" + fallbackName + ".nif");
                        if (charModel) {
                            nifName = fallbackName;
                        }
                    }
                }
                if (charModel) {
                    charModel->name = nifName;
                    parentNode->AttachChild(charModel.get());
                    outNodes.push_back(charModel);
                }
            }

            if (charModel) {
                charModel->local.translate.x = currentX;
                charModel->local.translate.y = lineY;
                charModel->local.scale = charScale;
                currentX += spacing;
                poolIndex++;
            } else {
                currentX += spacing * 0.5f;
            }

            i += bytesConsumed;
        }
        return currentX;
    }
}
