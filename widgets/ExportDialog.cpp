/* vi: set sw=4 ts=4:
 *
 * Copyright (C) 2014 Christian Hohnstaedt.
 *
 * All rights reserved.
 */


#include "ExportDialog.h"
#include "MainWindow.h"
#include "lib/base.h"

#include <QtWidgets/QComboBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QMessageBox>
#include <QtCore/QStringList>

ExportDialog::ExportDialog(QWidget *parent, QString fname, pki_base *pki)
	:QDialog(parent)
{
	setupUi(this);
	filename->setText(fname);
	setWindowTitle(XCA_TITLE);
	descr->setText(pki->getIntName());
	descr->setReadOnly(true);
}

void ExportDialog::on_fileBut_clicked()
{
	QString s = QFileDialog::getSaveFileName(this, QString(),
		filename->text(), filter, NULL,
		QFileDialog::DontConfirmOverwrite);

	if (!s.isEmpty()) {
		QDir::toNativeSeparators(s);
		filename->setText(s);
	}
}

void ExportDialog::on_exportFormat_activated(int selected)
{
	QString fn = filename->text();

	if (selected <0 || selected >= suffixes.size())
		return;

	foreach(QString suffix, suffixes) {
		if (fn.endsWith(QString(".") +suffix)) {
			fn = fn.left(fn.length() -suffix.length()) +
				suffixes[selected];
			break;
		}
	}
	filename->setText(fn);
}

bool ExportDialog::mayWriteFile(const QString &fname)
{
        if (QFile::exists(fname)) {
		xcaWarning msg(NULL,
			tr("The file: '%1' already exists!").arg(fname));
		msg.addButton(QMessageBox::Ok)->setText(
			tr("Overwrite"));
		msg.addButton(QMessageBox::Cancel)->setText(
			tr("Do not overwrite"));
		if (msg.exec() != QMessageBox::Ok)
	        {
			return false;
	        }
	}
	return true;
}

void ExportDialog::accept()
{
	if (mayWriteFile(filename->text()))
		QDialog::accept();
}

