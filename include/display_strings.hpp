#pragma once

// Text the firmware itself generates for the LED matrix, per `language` config
// key. Pure (no Arduino deps), host-tested in tests/host/display_strings_check.cpp.
//
// The matrix font is ASCII only, so every string here is written without
// diacritics and external text (weather descriptions) goes through ascii_fold().

#include <string>

enum class Lang { En, Fr, Pl, It, Es, Pt, De };

inline Lang parse_lang(const std::string& code)
{
    if (code == "fr") return Lang::Fr;
    if (code == "pl") return Lang::Pl;
    if (code == "it") return Lang::It;
    if (code == "es") return Lang::Es;
    if (code == "pt") return Lang::Pt;
    if (code == "de") return Lang::De;
    return Lang::En;
}

// Value of OpenWeatherMap's `lang=` query parameter for the language.
inline const char* owm_lang(Lang l)
{
    switch (l) {
        case Lang::Fr: return "fr";
        case Lang::Pl: return "pl";
        case Lang::It: return "it";
        case Lang::Es: return "es";
        case Lang::Pt: return "pt";
        case Lang::De: return "de";
        default:       return "en";
    }
}

struct DisplayStrings {
    const char* days[7];      // Sunday first (tm_wday)
    const char* sunrise;
    const char* sunset;
    const char* polar_day;
    const char* polar_night;
    const char* rebooting;
    const char* ap_connect;   // followed by "<hostname>-setup"
    const char* ap_browse;    // followed by the portal address
};

inline const DisplayStrings& display_strings(Lang l)
{
    static const DisplayStrings kEn = {
        {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"},
        "Sunrise", "Sunset", "Sun: polar day!", "Sun: polar night!",
        "Rebooting", "WiFi setup: connect to ", "then browse"};
    static const DisplayStrings kFr = {
        {"Dim","Lun","Mar","Mer","Jeu","Ven","Sam"},
        "Lever", "Coucher", "Soleil: jour polaire!", "Soleil: nuit polaire!",
        "Redemarrage", "Config WiFi: connectez-vous a ", "puis ouvrez"};
    static const DisplayStrings kPl = {
        {"Ndz","Pon","Wto","Sro","Czw","Pia","Sob"},
        "Wschod", "Zachod", "Slonce: dzien polarny!", "Slonce: noc polarna!",
        "Restart", "Konfiguracja WiFi: polacz z ", "potem otworz"};
    static const DisplayStrings kIt = {
        {"Dom","Lun","Mar","Mer","Gio","Ven","Sab"},
        "Alba", "Tramonto", "Sole: giorno polare!", "Sole: notte polare!",
        "Riavvio", "Config WiFi: connettiti a ", "poi apri"};
    static const DisplayStrings kEs = {
        {"Dom","Lun","Mar","Mie","Jue","Vie","Sab"},
        "Amanecer", "Ocaso", "Sol: dia polar!", "Sol: noche polar!",
        "Reiniciando", "Config WiFi: conectate a ", "luego abre"};
    static const DisplayStrings kPt = {
        {"Dom","Seg","Ter","Qua","Qui","Sex","Sab"},
        "Nascer", "Poente", "Sol: dia polar!", "Sol: noite polar!",
        "Reiniciando", "Config WiFi: ligue-se a ", "depois abra"};
    static const DisplayStrings kDe = {
        {"So","Mo","Di","Mi","Do","Fr","Sa"},
        "Sonnenaufgang", "Sonnenuntergang", "Sonne: Polartag!", "Sonne: Polarnacht!",
        "Neustart", "WLAN-Setup: verbinde mit ", "dann oeffne"};
    switch (l) {
        case Lang::Fr: return kFr;
        case Lang::Pl: return kPl;
        case Lang::It: return kIt;
        case Lang::Es: return kEs;
        case Lang::Pt: return kPt;
        case Lang::De: return kDe;
        default:       return kEn;
    }
}

// Fold UTF-8 Latin text to plain ASCII for the matrix font: Latin-1 letters
// (including German sharp s), the Polish letters, oe ligatures and curly
// apostrophes. Unknown multi-byte sequences pass through unchanged.
inline std::string ascii_fold(const char* in)
{
    // U+00C0..U+00FF, indexed by (second UTF-8 byte - 0x80)
    static const char* const kLatin1[64] = {
        "A","A","A","A","A","A","AE","C","E","E","E","E","I","I","I","I",
        "D","N","O","O","O","O","O","x","O","U","U","U","U","Y","Th","ss",
        "a","a","a","a","a","a","ae","c","e","e","e","e","i","i","i","i",
        "d","n","o","o","o","o","o","/","o","u","u","u","u","y","th","y"};

    std::string out;
    for (size_t i = 0; in[i]; ) {
        unsigned char c = (unsigned char)in[i];
        unsigned char d = c >= 0xC3 && in[i + 1] ? (unsigned char)in[i + 1] : 0;
        if (c == 0xC3 && d >= 0x80 && d <= 0xBF) {
            out += kLatin1[d - 0x80];
            i += 2;
            continue;
        }
        if ((c == 0xC4 || c == 0xC5) && d) {
            const char* r = nullptr;
            if (c == 0xC4) {
                switch (d) {
                    case 0x84: r = "A"; break; case 0x85: r = "a"; break;  // A/a ogonek
                    case 0x86: r = "C"; break; case 0x87: r = "c"; break;  // C/c acute
                    case 0x98: r = "E"; break; case 0x99: r = "e"; break;  // E/e ogonek
                }
            } else {
                switch (d) {
                    case 0x81: r = "L"; break; case 0x82: r = "l"; break;  // L/l stroke
                    case 0x83: r = "N"; break; case 0x84: r = "n"; break;  // N/n acute
                    case 0x92: r = "OE"; break; case 0x93: r = "oe"; break;
                    case 0x9A: r = "S"; break; case 0x9B: r = "s"; break;  // S/s acute
                    case 0xB9: r = "Z"; break; case 0xBA: r = "z"; break;  // Z/z acute
                    case 0xBB: r = "Z"; break; case 0xBC: r = "z"; break;  // Z/z dot
                    case 0xB8: r = "Y"; break;                             // Y diaeresis
                }
            }
            if (r) { out += r; i += 2; continue; }
        }
        // curly apostrophe U+2019 (E2 80 99)
        if (c == 0xE2 && in[i + 1] && in[i + 2] &&
            (unsigned char)in[i + 1] == 0x80 && (unsigned char)in[i + 2] == 0x99) {
            out += '\'';
            i += 3;
            continue;
        }
        out += in[i++];
    }
    return out;
}
