#include <KDesktopFile>
#include <QCoreApplication>
#include <QDebug>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2 || !KDesktopFile::isAuthorizedDesktopFile(QString::fromLocal8Bit(argv[1]))) {
        qCritical() << "KDE rejects the generated desktop launcher";
        return 1;
    }
    return 0;
}
