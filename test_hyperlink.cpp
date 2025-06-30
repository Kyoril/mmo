// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "src/shared/frame_ui/hyperlink.h"
#include <iostream>
#include <cassert>

using namespace mmo;

int main()
{
    // Test basic hyperlink parsing with brackets included
    std::string testText = "|cff0080ff|Hitem:12345|h[Sword of Power]|h|r";
    argb_t defaultColor = 0xFFFFFFFF;
    
    ParsedText result = ParseTextMarkup(testText, defaultColor);
    
    // Check that plain text includes the brackets
    std::cout << "Plain text: '" << result.plainText << "'" << std::endl;
    assert(result.plainText == "[Sword of Power]");
    
    // Check that we have one hyperlink
    assert(result.hyperlinks.size() == 1);
    
    // Check hyperlink properties
    const Hyperlink& link = result.hyperlinks[0];
    std::cout << "Hyperlink type: " << link.type << std::endl;
    std::cout << "Hyperlink payload: " << link.payload << std::endl;
    std::cout << "Hyperlink display: '" << link.displayText << "'" << std::endl;
    
    assert(link.type == "item");
    assert(link.payload == "12345");
    assert(link.displayText == "[Sword of Power]"); // Should include brackets
    assert(link.color == 0xff0080ff);
    
    // Check color changes
    assert(result.colorChanges.size() >= 1);
    assert(result.colorChanges[0].first == 0); // Color change at position 0
    assert(result.colorChanges[0].second == 0xff0080ff);
    
    std::cout << "Basic hyperlink test passed!" << std::endl;
    
    // Test multiple hyperlinks
    testText = "Check |cffff0000|Hitem:123|h[Sword]|h|r and |cff00ff00|Hspell:456|h[Fireball]|h|r";
    result = ParseTextMarkup(testText, defaultColor);
    
    std::cout << "Multiple hyperlinks plain text: '" << result.plainText << "'" << std::endl;
    assert(result.plainText == "Check [Sword] and [Fireball]");
    assert(result.hyperlinks.size() == 2);
    
    assert(result.hyperlinks[0].type == "item");
    assert(result.hyperlinks[0].payload == "123");
    assert(result.hyperlinks[0].displayText == "[Sword]");
    
    assert(result.hyperlinks[1].type == "spell");
    assert(result.hyperlinks[1].payload == "456");
    assert(result.hyperlinks[1].displayText == "[Fireball]");
    
    std::cout << "Multiple hyperlinks test passed!" << std::endl;
    
    // Test text without hyperlinks (color only)
    testText = "|cffff0000This is red text|r";
    result = ParseTextMarkup(testText, defaultColor);
    
    assert(result.plainText == "This is red text");
    assert(result.hyperlinks.size() == 0);
    assert(result.colorChanges.size() >= 1);
    
    std::cout << "Color-only test passed!" << std::endl;
    
    std::cout << "All hyperlink parsing tests passed successfully!" << std::endl;
    
    return 0;
}
