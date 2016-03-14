/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_FNC_SRC_STATUS_QT_H_
#define CS_FNC_SRC_STATUS_QT_H_

#include <QDebug>

#include <common/util/status.h>

QDebug operator<<(QDebug d, const util::Status &s);

util::Status QS(util::error::Code code, const QString &msg);
util::Status QSP(const QString &msg, util::Status s);

#endif /* CS_FNC_SRC_STATUS_QT_H_ */
