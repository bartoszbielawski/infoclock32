// Host test for include/lhc_text.hpp (LHC Page 1 comment and status text).
#include <cstdio>
#include <string>

#include <lhc_text.hpp>

static int failures = 0;

static void expect(const std::string& got, const std::string& want, const char* what)
{
    if (got != want) {
        printf("FAIL %s:\n  got  '%s'\n  want '%s'\n", what, got.c_str(), want.c_str());
        failures++;
    }
}

int main()
{
    const std::string B = std::string(" ") + kLhcLineBreak + " ";

    // The real comment from webfd on 2026-11-10
    expect(lhcHtmlToText("<center>  <br>  <br>So long LHC and thanks for all the collisions!"
                         "<br>Let&#8217;s follow the road to HiLumi LHC<br></center>"),
           "So long LHC and thanks for all the collisions!" + B + "Let's follow the road to HiLumi LHC",
           "real comment");

    expect(lhcHtmlToText(""), "", "empty");
    expect(lhcHtmlToText("<br/><BR /><p></p>"), "", "only breaks");
    expect(lhcHtmlToText("plain text"), "plain text", "plain");
    expect(lhcHtmlToText("  a \n\t b  "), "a b", "whitespace collapse");
    expect(lhcHtmlToText("one<br>two<p>three</p>four"), "one" + B + "two" + B + "three" + B + "four",
           "break tags");
    expect(lhcHtmlToText("<b>bold</b> and <font color=\"red\">red</font>"), "bold and red",
           "inline tags dropped, no break");
    expect(lhcHtmlToText("a<span>b</span>c"), "abc", "inline tag joins");

    // entities
    expect(lhcHtmlToText("A &amp; B &lt;3&gt; &quot;q&quot; &apos;s"), "A & B <3> \"q\" 's", "named basic");
    expect(lhcHtmlToText("a&nbsp;&nbsp;b"), "a b", "nbsp collapses");
    expect(lhcHtmlToText("1&ndash;2&mdash;3&hellip;"), "1-2-3...", "dashes, ellipsis");
    expect(lhcHtmlToText("&#8220;x&#8221; &#x2019;"), "\"x\" '", "numeric dec and hex");
    expect(lhcHtmlToText("&#233;t&eacute; caf&#xE9;"), "ete cafe", "accents folded");
    expect(lhcHtmlToText("Z&#243;&#322;w"), "Zolw", "Polish folded");
    expect(lhcHtmlToText("&amp;lt;"), "&lt;", "no double decode");
    expect(lhcHtmlToText("AT&T &unknown; & x &#;"), "AT&T &unknown; & x &#;", "bad entities kept");
    expect(lhcHtmlToText("&#128512;ok&#7;"), "ok", "emoji and control dropped");
    expect(lhcHtmlToText("x<b"), "x", "unterminated tag");
    expect(lhcHtmlToText(std::string("a") + kLhcLineBreak + "b"), "ab", "raw separator char removed");

    // status line
    expect(lhcStatusLine("SHUTDOWN", "NO BEAM", 0.0), "SHUTDOWN: NO BEAM", "status no energy");
    expect(lhcStatusLine("PROTON PHYSICS", "STABLE BEAMS", 6799.6),
           "PROTON PHYSICS: STABLE BEAMS @ 6800 GeV", "status with energy");
    expect(lhcStatusLine("MD", "", 450.0), "MD @ 450 GeV", "status without state");
    expect(lhcStatusLine("", "NO BEAM", 0.0), "", "status without mode");

    if (failures) {
        printf("lhc_text_check: %d failure(s)\n", failures);
        return 1;
    }
    printf("lhc_text_check passed\n");
    return 0;
}
