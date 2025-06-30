// Test example to verify hyperlink parsing works correctly
#include "src/shared/frame_ui/hyperlink.h"
#include <iostream>

int main()
{
    using namespace mmo;
    
    // Test the hyperlink parsing with brackets in display text
    std::string testText = "Click this |cFF00FF00|Hitem:12345|h[Epic Sword of Doom]|h|r for item info!";
    
    ParsedText result = ParseTextMarkup(testText, 0xFFFFFFFF);
    
    std::cout << "Original text: " << testText << std::endl;
    std::cout << "Parsed plain text: " << result.plainText << std::endl;
    std::cout << "Number of hyperlinks: " << result.hyperlinks.size() << std::endl;
    
    if (!result.hyperlinks.empty())
    {
        const auto& hyperlink = result.hyperlinks[0];
        std::cout << "Hyperlink type: " << hyperlink.type << std::endl;
        std::cout << "Hyperlink payload: " << hyperlink.payload << std::endl;
        std::cout << "Hyperlink display text: " << hyperlink.displayText << std::endl;
        std::cout << "Plain text start: " << hyperlink.plainTextStart << std::endl;
        std::cout << "Plain text end: " << hyperlink.plainTextEnd << std::endl;
    }
    
    return 0;
}
