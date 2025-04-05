// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Include asio before windows header files
#include "asio/io_service.hpp"

// Include windows header files
#include <Windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include "resource.h"

#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>

#include "asio.hpp"
#include "asio/ssl.hpp"

#include "base/macros.h"

// Global variables
HWND g_dialogHandle = NULL;
std::vector<std::string> g_logFiles;
std::string g_logContent;
std::string g_userInput;
std::mutex g_guiMutex;
bool g_isSending = false;
std::thread g_sendThread;

// Forward declarations
INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
bool SendErrorReport(const std::string& logContent, const std::string& userInput);
void LoadLogFiles();

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    // Parse command line arguments to get log file paths
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);

    // If no log files provided, silently terminate
    if (argc <= 1)
    {
        LocalFree(argvW);
        return 0;
    }

    // Store log file paths (convert wide strings to narrow strings)
    for (int i = 1; i < argc; ++i)
    {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, NULL, 0, NULL, NULL);
        std::string strArg(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, &strArg[0], size_needed, NULL, NULL);
        strArg.resize(size_needed - 1);  // Remove the null terminator from the string
        g_logFiles.push_back(strArg);
    }

    // Free the command line arguments
    LocalFree(argvW);

    // Initialize common controls
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    // Show the dialog
    DialogBoxA(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, MainDlgProc);

    // Wait for the send thread to terminate if it's running
    if (g_sendThread.joinable())
    {
        g_sendThread.join();
    }

    return 0;
}

// Load and concatenate log files
void LoadLogFiles()
{
    std::stringstream logStream;

    for (const auto& filePath : g_logFiles)
    {
        try
        {
            if (std::filesystem::exists(filePath))
            {
                std::ifstream file(filePath);
                if (file)
                {
                    // Add a header for the log file with a separator line
                    logStream << "=================================================================\n";
                    logStream << "=== Log File: " << filePath << "\n";
                    logStream << "=================================================================\n\n";
                    
                    // Read the file line by line to ensure proper formatting
                    std::string line;
                    while (std::getline(file, line))
                    {
                        // Replace tabs with spaces for better alignment
                        std::string formattedLine = line;
                        size_t pos = 0;
                        while ((pos = formattedLine.find('\t', pos)) != std::string::npos)
                        {
                            formattedLine.replace(pos, 1, "    ");
                            pos += 4;
                        }
                        
                        logStream << formattedLine << "\r\n";
                    }
                    
                    logStream << "\n\n";
                }
                else
                {
                    logStream << "Error: Could not open file " << filePath << "\r\n\r\n";
                }
            }
            else
            {
                logStream << "Error: File does not exist " << filePath << "\r\n\r\n";
            }
        }
        catch (const std::exception& e)
        {
            logStream << "Exception while processing file " << filePath << ": " << e.what() << "\r\n\r\n";
        }
    }

    g_logContent = logStream.str();
}

// Send error report to server
bool SendErrorReport(const std::string& logContent, const std::string& userInput)
{
    try
    {
        // Update status
        SetDlgItemTextA(g_dialogHandle, IDC_STATUS_LABEL, "Sending error report...");

        // Create ASIO context with SSL support
        asio::io_service io_service;

        // Use TLS 1.2 which is more widely supported
        asio::ssl::context ctx(asio::ssl::context::tlsv12_client);

        // Don't verify the certificate for now (similar to how the launcher works)
        ctx.set_verify_mode(asio::ssl::verify_none);

        // Create HTTPS client
        asio::ssl::stream<asio::ip::tcp::socket> socket(io_service, ctx);
        asio::ip::tcp::resolver resolver(io_service);

        // Connect to server
        const std::string serverHost = "error.mmo-dev.net";
        const std::string serverPath = "/api/reports";

        // Resolve the server hostname - use port 443 explicitly for HTTPS
        auto endpoints = resolver.resolve(serverHost, "443");
        asio::connect(socket.lowest_layer(), endpoints);
        socket.lowest_layer().set_option(asio::ip::tcp::no_delay(true));

        // Perform SSL handshake without verification (for compatibility)
        socket.handshake(asio::ssl::stream_base::client);

        // Prepare HTTP POST request with the log content and user input
        std::stringstream requestStream;
        std::string boundary = "----WebKitFormBoundaryABC123";
        std::string postData;

        // CRITICAL FIX: The server expects 'log_content', not 'crashReport'
        postData += "--" + boundary + "\r\n";
        postData += "Content-Disposition: form-data; name=\"log_content\"; filename=\"crash-report.txt\"\r\n";
        postData += "Content-Type: text/plain\r\n\r\n";
        postData += logContent + "\r\n";

        // CRITICAL FIX: The server expects 'user_input', not 'additionalInfo'
        postData += "--" + boundary + "\r\n";
        postData += "Content-Disposition: form-data; name=\"user_input\"\r\n\r\n";
        postData += userInput + "\r\n";

        // Add metadata fields
        postData += "--" + boundary + "\r\n";
        postData += "Content-Disposition: form-data; name=\"appVersion\"\r\n\r\n";
        postData += "0.2.0.1281\r\n";

        postData += "--" + boundary + "\r\n";
        postData += "Content-Disposition: form-data; name=\"osVersion\"\r\n\r\n";
        postData += "Windows 10\r\n";

        postData += "--" + boundary + "\r\n";
        postData += "Content-Disposition: form-data; name=\"deviceModel\"\r\n\r\n";
        postData += "Test Device\r\n";

        postData += "--" + boundary + "\r\n";
        postData += "Content-Disposition: form-data; name=\"errorMessage\"\r\n\r\n";
        postData += "Test Error Message\r\n";

        postData += "--" + boundary + "\r\n";
        postData += "Content-Disposition: form-data; name=\"userEmail\"\r\n\r\n";
        postData += "test@example.com\r\n";

        // End of form data
        postData += "--" + boundary + "--\r\n";

        // Build the HTTP request
        requestStream << "POST " << serverPath << " HTTP/1.1\r\n";
        requestStream << "Host: " << serverHost << "\r\n";
        requestStream << "User-Agent: MMORPG Error Reporter\r\n";
        requestStream << "Content-Type: multipart/form-data; boundary=" << boundary << "\r\n";
        requestStream << "Content-Length: " << postData.length() << "\r\n";
        requestStream << "Connection: close\r\n\r\n";
        requestStream << postData;

        // Send the request
        asio::write(socket, asio::buffer(requestStream.str()));

        // Read the response
        asio::streambuf response;
        asio::read_until(socket, response, "\r\n");

        // Check if we got a successful response
        std::istream response_stream(&response);
        std::string http_version;
        response_stream >> http_version;
        unsigned int status_code;
        response_stream >> status_code;

        std::ostringstream responseMessage;
        std::string status_message;
        std::getline(response_stream, status_message);

        // Close the connection
        socket.lowest_layer().close();

        // Return success if status code is 201 CREATED
        return (status_code == 201);
    }
    catch (const std::exception& e)
    {
        // Update status with error message
        std::string errorMsg = "Error: ";
        errorMsg += e.what();
        SetDlgItemTextA(g_dialogHandle, IDC_STATUS_LABEL, errorMsg.c_str());
        return false;
    }
}

// Dialog procedure
INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        {
            g_dialogHandle = hDlg;

            // Load log files
            LoadLogFiles();

            // Set a monospaced font for better log readability
            HFONT hFont = CreateFont(
                14,                     // Height
                0,                      // Width (0 = default)
                0,                      // Escapement
                0,                      // Orientation
                FW_NORMAL,              // Weight
                FALSE,                  // Italic
                FALSE,                  // Underline
                FALSE,                  // StrikeOut
                ANSI_CHARSET,           // CharSet
                OUT_DEFAULT_PRECIS,     // OutPrecision
                CLIP_DEFAULT_PRECIS,    // ClipPrecision
                DEFAULT_QUALITY,        // Quality
                FIXED_PITCH | FF_MODERN,// PitchAndFamily
                "Consolas");            // Face name (monospaced font)
            
            // Apply the font to the error text control
            SendDlgItemMessage(hDlg, IDC_ERROR_TEXT, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            // Set log content in the text box
            SetDlgItemTextA(hDlg, IDC_ERROR_TEXT, g_logContent.c_str());

            return TRUE;
        }

    case WM_CLOSE:
        EndDialog(hDlg, 0);
        return TRUE;

    case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case IDC_CLOSE:
                {
                    EndDialog(hDlg, 0);
                    return TRUE;
                }

            case IDC_SEND:
                {
                    // Prevent multiple sends
                    if (g_isSending)
                    {
                        return TRUE;
                    }

                    // Get user input
                    char buffer[4096] = {0};
                    GetDlgItemTextA(hDlg, IDC_USER_INPUT, buffer, sizeof(buffer));
                    g_userInput = buffer;

                    // Disable send button
                    EnableWindow(GetDlgItem(hDlg, IDC_SEND), FALSE);
                    g_isSending = true;

                    // Start send thread
                    if (g_sendThread.joinable())
                    {
                        g_sendThread.join();
                    }

                    g_sendThread = std::thread([hDlg]() {
                        bool success = SendErrorReport(g_logContent, g_userInput);

                        // Update UI
                        std::scoped_lock<std::mutex> lock(g_guiMutex);
                        if (success)
                        {
                            SetDlgItemTextA(hDlg, IDC_STATUS_LABEL, "Error report sent successfully!");
                            // Close dialog after successful send
                            PostMessage(hDlg, WM_CLOSE, 0, 0);
                        }
                        else
                        {
                            SetDlgItemTextA(hDlg, IDC_STATUS_LABEL, "Failed to send error report.");
                            // Re-enable send button
                            EnableWindow(GetDlgItem(hDlg, IDC_SEND), TRUE);
                        }
                        g_isSending = false;
                    });

                    return TRUE;
                }
            }
            break;
        }

    case WM_DESTROY:
        {
            PostQuitMessage(0);
            return TRUE;
        }
    }

    return FALSE;
}
