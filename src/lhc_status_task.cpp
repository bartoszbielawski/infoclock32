

#include <pgmspace.h>
#include <http_utils.hpp>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <data_store.hpp>
#include <logger.hpp>
#include <string_utils.h>
#include <string>

static const char pageUrl[] PROGMEM = "https://alicedcs.web.cern.ch/monitoring/screenshots/rss.xml";

static std::map<std::string, std::string> interesting_fields =
{
    {"LhcPage1", ""},
    {"LhcBeamMode", ""},
    {"BeamEnergy", ""},
    {"LhcMachineMode", ""}
};

/**
 * @brief Normalizes and strips HTML-like markup from a mutable String.
 *
 * This function cleans the input text in several stages:
 * 1. Replaces known line-break tags (`<br>` and `<br/>`) with `" - "` as a separator.
 * 2. Collapses repeated separators (`" -  - "`) into a single `" - "`.
 * 3. Collapses multiple spaces (`"  "`) into single spaces.
 * 4. Removes a trailing separator pattern (`" -"`) if present at the end.
 * 5. Removes any remaining content enclosed in angle brackets by scanning characters
 *    and ignoring text while inside a tag (`<...>`).
 *
 * @param str Reference to the input String; modified in place to contain cleaned plain text.
 *
 * @note Tag stripping is character-based and simple: it does not validate HTML structure.
 * @note The trailing separator cleanup uses global replace semantics, which may affect
 *       other `" -"` occurrences depending on String::replace behavior.
 */
void removeHTMLTags(String& str)
{
    str.replace("<br>", " - ");
    str.replace("<br/>", " - ");

    // Strip remaining HTML tags
    String result;
    bool inTag = false;
    for (unsigned int i = 0; i < str.length(); i++)
    {
        char c = str[i];
        if (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag) result += c;
    }
    str = result;

    str.trim();

    str.replace(" -  - ", " - ");
    str.replace("  ", " ");
    if (str.endsWith(" -"))
        str.replace(" -", ""); //remove trailing separator if exists        
}



void lhc_status_task(void *parameter)
{
    std::string modeAndEnergyMessage;
    std::string page1Message;

    time_t last_update = 0;

    auto& rmd = ResourceManager<LMDS>::getInstance();
    auto& matrix = rmd.getResourceRef();

    while (true)
    {
        if (difftime(time(nullptr), last_update) > 30)
        {
            String output;
            auto response = HttpUtils::httpGet(pageUrl, output, true);
            if (response != 200)
            {
                logPrintf("LHC", "HTTP GET failed, response: %d", response);
                vTaskDelay(300 * 1000 / portTICK_PERIOD_MS); // wait a minute before
                continue;
            }      
            
            bool fields_updated = false;

            StringViewStream svs(output);  
            while (svs.available())
            {
                String line = svs.readStringUntil('\n');
                line.trim();
                if (not line.startsWith("<title>"))
                    continue; //line doesn't contain what we want
                
                int colonIndex = line.indexOf(':');
                if (colonIndex == -1)
                    continue; //the line has no colon, skip it too

                //extract title
                String title = line.substring(0, colonIndex);
                title.replace("<title>", "");

                //if the title is one of the interesting fields, extract the value and save back to the map
                if (interesting_fields.find(title.c_str()) != interesting_fields.end())
                {
                    String value = line.substring(colonIndex + 1);
                    removeHTMLTags(value);
                    value.replace("</title>", "");
                    value.trim();
                    interesting_fields[title.c_str()] = value.c_str();
                    logPrintf("LHC", "%s = %s", title.c_str(), value.c_str());
                    fields_updated = true;
                }
            }

            if (fields_updated)
            {
                //create message to be displayed
                char buffer[128];
                snprintf_P(buffer, sizeof(buffer), PSTR("%s: %s @ %s"),
                    interesting_fields["LhcMachineMode"].c_str(),
                    interesting_fields["LhcBeamMode"].c_str(),
                    interesting_fields["BeamEnergy"].c_str());
                
                modeAndEnergyMessage = buffer;
                    
                page1Message = interesting_fields["LhcPage1"];
                last_update = time(nullptr);
            }
        }   //end of update block

        if (not rmd.make_access_request())
        {
            logPrintf("LHC", "Failed to get access to display");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        if (not modeAndEnergyMessage.empty())
        {
            scrollMessage(modeAndEnergyMessage, matrix, 20);
        }
        rmd.release_access();

        vTaskDelay(5000 / portTICK_PERIOD_MS);
        if (not rmd.make_access_request())
        {
            logPrintf("LHC", "Failed to get access to display");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        if (not page1Message.empty())
        {
            logPrintf("LHC", "Page1: %s", page1Message.c_str());
            scrollMessage(page1Message, matrix, 20);
        }
        rmd.release_access();
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}