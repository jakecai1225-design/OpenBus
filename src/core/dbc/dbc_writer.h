#ifndef DBC_WRITER_H
#define DBC_WRITER_H

#include "core/dbcdata.h"
#include <QString>

namespace dbc {

/// 将 DbcFile 序列化写回 .dbc 文本文件
/// @return 成功返回 true
bool write(const QString &filePath, const DbcFile &file);

} // namespace dbc

#endif // DBC_WRITER_H
