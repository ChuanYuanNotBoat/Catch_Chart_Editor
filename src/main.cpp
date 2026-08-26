#include "app/Application.h"
#include "app/PlaybackBenchmarkRunner.h"
#include "utils/Logger.h"
#include <QCommandLineParser>
#include <QDebug>
#include <QTimer>
#include <iostream>

int main(int argc, char *argv[])
{
    try
    {
        // Set application name and version.
        QCoreApplication::setApplicationName("Malody Catch Chart Editor");
        QCoreApplication::setApplicationVersion("Beta v1.11.0");

        Application app(argc, argv);

        QCommandLineParser parser;
        parser.setApplicationDescription("Malody Catch Chart Editor");
        parser.addHelpOption();
        parser.addVersionOption();
        PlaybackBenchmarkRunner::addCommandLineOptions(parser);
        parser.process(app);

        const bool benchmarkRequested = PlaybackBenchmarkRunner::isRequested(parser);
        PlaybackBenchmarkOptions benchmarkOptions;
        if (benchmarkRequested)
        {
            QString optionError;
            if (!PlaybackBenchmarkRunner::optionsFromParser(parser, &benchmarkOptions, &optionError))
            {
                std::cerr << optionError.toStdString() << std::endl;
                return 4;
            }
        }

        if (!app.initialize())
        {
            Logger::error("Failed to initialize application.");
            Logger::shutdown();
            return 1;
        }

        int result = 0;
        if (benchmarkRequested)
        {
            PlaybackBenchmarkRunner benchmarkRunner(&app, benchmarkOptions);
            QObject::connect(&benchmarkRunner, &PlaybackBenchmarkRunner::finished,
                             &app, [&app](int exitCode)
                             {
                                 app.exit(exitCode);
                             });
            QTimer::singleShot(0, &benchmarkRunner, &PlaybackBenchmarkRunner::start);
            result = app.exec();
        }
        else
        {
            result = app.exec();
        }

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
