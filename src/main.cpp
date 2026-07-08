#include "app/Application.h"
#include "utils/Logger.h"
#include <QDebug>
#include <iostream>

int main(int argc, char *argv[])
{
    try
    {
        // Set application name and version.
        QCoreApplication::setApplicationName("Malody Catch Chart Editor");
        QCoreApplication::setApplicationVersion("Beta v1.10.3");

        Application app(argc, argv);

        if (!app.initialize())
        {
            Logger::error("Failed to initialize application.");
            Logger::shutdown();
            return 1;
        }

        int result = app.exec();

        Logger::info("Application exiting with code: " + QString::number(result));
        Logger::shutdown();

        return result;
    }
    catch (const std::exception &e)
    {
        Logger::error("Exception: " + QString::fromStdString(std::string(e.what())));
        Logger::shutdown();
        std::cerr << "Exception: " << e.what() << std::endl;
        return 2;
    }
    catch (...)
    {
        Logger::error("Unknown exception occurred");
        Logger::shutdown();
        std::cerr << "Unknown exception occurred" << std::endl;
        return 3;
    }
}
