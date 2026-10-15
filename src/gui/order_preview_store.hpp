#pragma once
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QSaveFile>
namespace cps::gui {
inline QString orderCoverPath(const QString& directory,const QString& id) {
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9_-]{1,80}$"));
    return !directory.isEmpty()&&safe.match(id).hasMatch() ? QDir(directory).absoluteFilePath(QStringLiteral("order-previews/")+id+QStringLiteral(".png")) : QString();
}
inline bool saveOrderCover(const QString& directory,const QString& id,const QImage& image) {
    const QString path=orderCoverPath(directory,id);
    if(path.isEmpty()||image.isNull()||!QDir().mkpath(QFileInfo(path).absolutePath()))return false;
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly)&&image.scaled(QSize(1200,1600),Qt::KeepAspectRatio,Qt::SmoothTransformation).save(&file,"PNG")&&file.commit();
}
}
