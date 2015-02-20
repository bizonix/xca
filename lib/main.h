/* vi: set sw=4 ts=4:
 *
 * Copyright (C) 2001 - 2015 Christian Hohnstaedt.
 *
 * All rights reserved.
 */

#ifndef __MAIN_H
#define __MAIN_H

#include <QtWidgets/QApplication>
#include <QtWidgets/QAction>
#include <QtCore/QTranslator>
#include <QtCore/QLocale>
#include "widgets/MainWindow.h"
#ifdef WIN32
#include <windows.h>
#endif

class XcaTranslator : public QTranslator
{
	Q_OBJECT
public:
	XcaTranslator(QObject *p = NULL) : QTranslator(p) { }
	bool load(const QLocale &locale, const QString &filename,
		const QString &dir)
	{
#if 0
		return QTranslator::load(locale, filename, "_", dir, ".qm");
#else
		return QTranslator::load(QString("%1_%2").arg(filename).arg(locale.name()), dir);
#endif
	}
};

class XCA_application : public QApplication
{
	Q_OBJECT

private:
	MainWindow *mainw;
	XcaTranslator *qtTr;
	XcaTranslator *xcaTr;
	static QLocale lang;

public:
	XCA_application(int &argc, char *argv[]);
	void setMainwin(MainWindow *m);
	void setupLanguage(QLocale lang);
	static QLocale language() { return lang; }
	static QFont tableFont;

protected:
	bool event(QEvent *ev);

signals:
	void openFiles(QStringList &);

public slots:
	void switchLanguage(QAction* a);
};
#endif
