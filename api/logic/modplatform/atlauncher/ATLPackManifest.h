#pragma once

#include <QString>
#include <QVector>
#include <QJsonObject>
#include <multiservermc_logic_export.h>

namespace ATLauncher
{

enum class PackType
{
    Public,
    Private
};

enum class ModType
{
    Root,
    Forge,
    Jar,
    Mods,
    Flan,
    Dependency,
    Ic2Lib,
    DenLib,
    Coremods,
    MCPC,
    Plugins,
    Extract,
    Decomp,
    TexturePack,
    ResourcePack,
    ShaderPack,
    TexturePackExtract,
    ResourcePackExtract,
    Millenaire,
    Unknown
};

enum class DownloadType
{
    Server,
    Browser,
    Direct,
    Unknown
};

struct VersionLoader
{
    QString type;
    bool latest;
    bool recommended;
    bool choose;

    QString version;
};

struct VersionLibrary
{
    QString url;
    QString file;
    QString server;
    QString md5;
    DownloadType download;
    QString download_raw;
};

struct VersionMod
{
    QString name;
    QString version;
    QString url;
    QString file;
    QString md5;
    DownloadType download;
    QString download_raw;
    ModType type;
    QString type_raw;

    ModType extractTo;
    QString extractTo_raw;
    QString extractFolder;

    ModType decompType;
    QString decompType_raw;
    QString decompFile;

    QString description;
    bool optional;
    bool recommended;
    bool selected;
    bool hidden;
    bool library;
    QString group;
    QVector<QString> depends;

    bool client;

    // computed
    bool effectively_hidden;
};

struct VersionConfigs
{
    int filesize;
    QString sha1;
};

struct PackVersion
{
    QString version;
    QString minecraft;
    bool noConfigs;
    QString mainClass;
    QString extraArguments;

    VersionLoader loader;
    QVector<VersionLibrary> libraries;
    QVector<VersionMod> mods;
    VersionConfigs configs;
};

MULTISERVERMC_LOGIC_EXPORT void loadVersion(PackVersion & v, QJsonObject & obj);

}
