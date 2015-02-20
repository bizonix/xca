/* vi: set sw=4 ts=4:
 *
 * Copyright (C) 2001 - 2015 Christian Hohnstaedt.
 *
 * All rights reserved.
 */

#ifdef WIN32
#include <windows.h>
#else
#include <netinet/in.h>
#endif

#include "db_base.h"
#include "func.h"
#include "exception.h"
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QListView>
#include <QtGui/QClipboard>
#include <QtCore/QDir>
#include <QtCore/QDebug>
#include "widgets/MainWindow.h"
#include "widgets/ImportMulti.h"

db_base::db_base(QString db, MainWindow *mw)
	:QAbstractItemModel(NULL)
{
	dbName = db;
	rootItem = newPKI();
	mainwin = mw;
	colResizing = 0;
	currentIdx = QModelIndex();
	class_name = "base";
}

db_base::~db_base()
{
	saveHeaderState();
	delete rootItem;
}

pki_base *db_base::newPKI(db_header_t *)
{
	return new pki_base("rootItem");
}

void db_base::createSuccess(pki_base *pki)
{
	if (!pki)
		return;

	if (pki_base::suppress_messages)
		return;

	XCA_INFO(pki->getMsg(pki_base::msg_create).arg(pki->getIntName()));
}

void db_base::remFromCont(QModelIndex &idx)
{
	if (!idx.isValid())
		return;
	pki_base *pki = static_cast<pki_base*>(idx.internalPointer());
	pki_base *parent_pki = pki->getParent();
	int row = pki->row();

	beginRemoveRows(parent(idx), row, row);
	parent_pki->takeChild(pki);
	endRemoveRows();
	emit columnsContentChanged();
}

int db_base::handleBadEntry(unsigned char *p, db_header_t *head)
{
	QString name = QString::fromUtf8(head->name);
	QString txt = tr("Bad database item\nName: %1\nType: %2\nSize: %3\n%4")
		.arg(name).arg(class_name)
		.arg(head->len - sizeof(db_header_t))
		.arg(tr("Do you want to delete the item from the database? The bad item may be extracted into a separate file."));

	xcaWarning msg(mainwin, txt);
	msg.addButton(QMessageBox::Ok)->setText(tr("Delete"));
	msg.addButton(QMessageBox::Apply)->setText(tr("Delete and extract"));
	msg.addButton(QMessageBox::Cancel)->setText(tr("Continue"));

	switch (msg.exec()) {
	case QMessageBox::Ok:
		return 1;
	case QMessageBox::Cancel:
	default:
		return 0;
	case QMessageBox::Apply:
		break;
	}

	QString s = QFileDialog::getSaveFileName(mainwin, QString(), name,
			QString(), NULL, QFileDialog::DontConfirmOverwrite);

	size_t l;
	db_header_t h;
	FILE *fp = fopen(QString2filename(s), "w");
	if (!fp) {
		throw errorEx(tr("Error opening file: '%1': %2").
                        arg(s).arg(strerror(errno)), class_name);
	}

	h.magic   = ntohl(head->magic);
	h.len     = ntohl(head->len);
	h.headver = ntohs(head->headver);
	h.type    = ntohs(head->type);
	h.version = ntohs(head->version);
	h.flags   = ntohs(head->flags);
	memcpy(h.name, head->name, NAMELEN);

	l = fwrite(&h, sizeof h, 1, fp);
	l += fwrite(p, head->len - sizeof h, 1, fp);
	fclose(fp);

	return (l == 2);
}

void db_base::loadContainer()
{
	db mydb(dbName);
	unsigned char *p = NULL;
	db_header_t head;
	pki_base *pki;

	for (int i=0; i < pkitype.count(); i++) {
		mydb.first();
		while (mydb.find(pkitype[i], QString()) == 0) {
			QString s;
			p = mydb.load(&head);
			if (!p) {
				qWarning("Load was empty !");
				goto next;
			}
			pki = newPKI(&head);
			if (pki->getVersion() < head.version) {
				qWarning("Item[%s]: Version %d "
					"> known version: %d -> ignored",
					head.name, head.version,
					pki->getVersion()
				);
				free(p);
				delete pki;
				goto next;
			}
			pki->setIntName(QString::fromUtf8(head.name));

			try {
				pki->fromData(p, &head);
			}
			catch (errorEx &err) {
				err.appendString(pki->getIntName());
				mainwin->Error(err);
				delete pki;
				pki = NULL;
				try {
					if (handleBadEntry(p, &head)) {
						mydb.erase();
					}
				} catch (errorEx &err) {
					mainwin->Error(err);
				}
			}
			free(p);
			if (pki) {
				inToCont(pki);
			}
next:
			if (mydb.next())
				break;
		}
	}

	mydb.first();
	if (!mydb.find(setting, class_name + "_hdView")) {
		QByteArray ba;
		char *p;
		if ((p = (char *)mydb.load(&head))) {
			ba = QByteArray(p, head.len - sizeof(db_header_t));
			free(p);
		}
		if (head.version != 5)
			return;
		try {
			allHeaders.fromData(ba);
		}  catch (errorEx()) {
			for (int i=0; i< allHeaders.count(); i++) {
				allHeaders[i]->reset();
			}
		}
	}
	emit columnsContentChanged();
	return;
}

void db_base::updateHeaders()
{
	QByteArray ba = allHeaders.toData();
	foreach(dbheader *h, allHeaders)
		delete h;
	allHeaders = getHeaders();
	allHeaders.fromData(ba);
}

dbheaderList db_base::getHeaders()
{
	dbheaderList h;
	/* "No." handled in XcaProxyModel */
	h << new dbheader(HD_internal_name, true, tr("Internal name"))
	  << new dbheader(HD_counter, false, tr("No."));
	return h;
}


void db_base::saveHeaderState()
{
	QByteArray ba;
	if (dbName.isEmpty())
		return;
	ba = allHeaders.toData();
	db mydb(dbName);
	mydb.set((const unsigned char *)ba.constData(), ba.size(), 5,
		setting, class_name + "_hdView");
}

void db_base::setVisualIndex(int i, int visualIndex)
{
	if (colResizing)
		return;
	allHeaders[i]->visualIndex = visualIndex;
}

void db_base::sectionResized(int i, int,  int newSize)
{
	if (!allHeaders[i]->show || newSize <= 0 || colResizing)
		return;
	allHeaders[i]->size = newSize;
}

bool db_base::fixedHeaderSize(int sect)
{
	return allHeaders[sect]->size != -1;
}

void db_base::initHeaderView(QHeaderView *hv)
{
	int max = allHeaders.count();
	colResizeStart();
	for (int i=0; i<max; i++) {
		allHeaders[i]->setupHeaderView(i, hv);
	}
	for (int i=0; i<max; i++) {
		if (allHeaders[i]->visualIndex == -1)
			continue;
		if (hv->visualIndex(i) != allHeaders[i]->visualIndex) {
			hv->moveSection(hv->visualIndex(i),
					allHeaders[i]->visualIndex);
		}
	}
	colResizeEnd();
}

void db_base::sortIndicatorChanged(int logicalIndex, Qt::SortOrder order)
{
	int max = allHeaders.count();
	for (int i=0; i<max; i++) {
		allHeaders[i]->sortIndicator = -1;
	}
	allHeaders[logicalIndex]->sortIndicator = order;
}

void db_base::insertPKI(pki_base *pki)
{
	QString name;
	db mydb(dbName);
	QByteArray ba = pki->toData();

	if (ba.count() > 0) {
		name = mydb.uniq_name(pki->getIntName(), pkitype);
		pki->setIntName(name);
		mydb.add((const unsigned char*)ba.constData(), ba.count(),
			pki->getVersion(), pki->getType(), name);
	}
	inToCont(pki);
	emit columnsContentChanged();
}

void db_base::delete_ask()
{
	if (!currentIdx.isValid())
		return;
	pki_base *pki = static_cast<pki_base*>(currentIdx.internalPointer());

	if (!XCA_OKCANCEL(pki->getMsg(pki_base::msg_delete).arg(pki->getIntName())))
		 return;

	deletePKI();
}

void db_base::pem2clipboard()
{
	long l;
        const char *p;
	QString msg;
	QClipboard *cb = QApplication::clipboard();

	BIO *bio = BIO_new(BIO_s_mem());
	if (!currentIdx.isValid())
		return;
	pki_base *pki = static_cast<pki_base*>(currentIdx.internalPointer());
	pki->pem(bio);
	openssl_error();
	l = BIO_get_mem_data(bio, &p);
	msg = QString::fromUtf8(p, l);
	BIO_free(bio);
	if (cb->supportsSelection())
		cb->setText(msg, QClipboard::Selection);
	else
		cb->setText(msg);
}

void db_base::deletePKI()
{
	if (!currentIdx.isValid())
		return;
	pki_base *pki = static_cast<pki_base*>(currentIdx.internalPointer());
	try {
		try {
			pki->deleteFromToken();
		} catch (errorEx &err) {
			MainWindow::Error(err);
		}

		remFromCont(currentIdx);

		db mydb(dbName);
		mydb.find(pki->getType(), pki->getIntName());
		mydb.erase();
		delete pki;
	} catch (errorEx &err) {
		MainWindow::Error(err);
	}
}

void db_base::updatePKI(pki_base *pki)
{
	db mydb(dbName);

	QByteArray ba = pki->toData();

	if (ba.count() > 0) {
		mydb.set((const unsigned char*)ba.constData(), ba.count(),
			pki->getVersion(), pki->getType(), pki->getIntName());
	}
}

void db_base::deleteSelectedItems(XcaTreeView* view)
{
	QModelIndexList indexes = view->getSelectedIndexes();
	QModelIndex index;
	QString items, msg;
	int count = 0;
	pki_base *pki = NULL;

	if (indexes.count() == 0)
		return;

	foreach(index, indexes) {
		if (index.column() != 0)
			continue;
		pki = static_cast<pki_base*>(index.internalPointer());
		items += "'" + pki->getIntName() + "' ";
		count++;
	}
	if (count == 1)
		msg = pki->getMsg(pki_base::msg_delete).arg(pki->getIntName());
	else
		msg = pki->getMsg(pki_base::msg_delete_multi).arg(count).
				arg(items);

	if (!XCA_OKCANCEL(msg))
		return;

	foreach(index, indexes) {
		if (index.column() != 0)
			continue;
		currentIdx = index;
		deletePKI();
	}
	currentIdx = QModelIndex();
}

void db_base::showSelectedItems(XcaTreeView* view)
{
	QModelIndexList indexes = view->getSelectedIndexes();
	QModelIndex index;
	QString items;

	foreach(index, indexes) {
		if (index.column() != 0)
			continue;
		currentIdx = index;
		showItem();
	}
	currentIdx = QModelIndex();
}

void db_base::storeSelectedItems(XcaTreeView* view)
{
	QModelIndexList indexes = view->getSelectedIndexes();
	QModelIndex index;
	QString items;

	foreach(index, indexes) {
		if (index.column() != 0)
			continue;
		try {
			currentIdx = index;
			store();
		} catch (errorEx &err) {
			MainWindow::Error(err);
		}

	}
	currentIdx = QModelIndex();
}

void db_base::showItem(const QModelIndex &index)
{
	showPki(static_cast<pki_key*>(index.internalPointer()));
}

void db_base::showItem(const QString name)
{
	pki_base *pki = getByName(name);
	if (pki)
		showPki(pki);
}

void db_base::insertChild(pki_base *parent, pki_base *child)
{
	QModelIndex idx = QModelIndex();

	if (parent == child || parent == NULL)
		parent = rootItem;

	if (parent != rootItem)
		idx = index(parent);

	beginInsertRows(idx, 0, 0);
	parent->insert(0,child);
	endInsertRows();
}

void db_base::inToCont(pki_base *pki)
{
	insertChild(rootItem, pki);
}

pki_base *db_base::getByName(QString desc)
{
	FOR_ALL_pki(pki, pki_base) {
		if (pki->getIntName() == desc)
			return pki;
	}
	return NULL;
}

pki_base *db_base::getByReference(pki_base *refpki)
{
	if (refpki == NULL)
		return NULL;
	FOR_ALL_pki(pki, pki_base) {
		if (refpki->compare(pki))
			return pki;
	}
	return NULL;
}

QStringList db_base::getDesc()
{
	QStringList x;
	x.clear();
	FOR_ALL_pki(pki, pki_base) {
		x.append(pki->getIntName());
	}
	return x;
}

pki_base *db_base::insert(pki_base *item)
{
	insertPKI(item);
	return item;
}

void db_base::writeAll(void)
{
}

void db_base::dump(QString dirname)
{
	dirname += QDir::separator() + class_name;
	QDir d(dirname);
	if (!d.exists() && !d.mkdir(dirname)) {
		throw errorEx("Could not create directory '" + dirname + "'");
	}

	try {
		FOR_ALL_pki(pki, pki_base) {
			pki->writeDefault(dirname);
		}
	}
	catch (errorEx &err) {
		mainwin->Error(err);
	}
}

QModelIndex db_base::index(int row, int column, const QModelIndex &parent)
	                const
{
	pki_base *parentItem;

	if (!parent.isValid())
		parentItem = rootItem;
	else
		parentItem = static_cast<pki_base*>(parent.internalPointer());

	pki_base *childItem = parentItem->child(row);
	if (childItem)
		return createIndex(row, column, childItem);
	else
		return QModelIndex();
}

QModelIndex db_base::index(pki_base *pki) const
{
	return createIndex(pki->row(), 0, pki);
}

QModelIndex db_base::parent(const QModelIndex &idx) const
{
	if (!idx.isValid())
		return QModelIndex();

	pki_base *childItem = static_cast<pki_base*>(idx.internalPointer());
	pki_base *parentItem = childItem->getParent();
#if 0
	if (parentItem == NULL) {
		printf("Item: (%s) %s: parent == NULL\n",
			childItem->className(), CCHAR(childItem->getIntName()));
	}
	printf("Parent of:%s(%p) %p %p\n",
	CCHAR(childItem->getIntName()), childItem, parentItem, rootItem);
#endif
	if (parentItem == rootItem || parentItem == NULL)
		return QModelIndex();

	return index(parentItem);
}

int db_base::rowCount(const QModelIndex &parent) const
{
	pki_base *parentItem;

	if (!parent.isValid())
		parentItem = rootItem;
	else
		parentItem = static_cast<pki_base*>(parent.internalPointer());

	//printf("%s rows=%d\n", CCHAR(parentItem->getIntName()), parentItem->childCount());
	return parentItem->childCount();
}

int db_base::columnCount(const QModelIndex &) const
{
	return allHeaders.count();
}

QVariant db_base::data(const QModelIndex &index, int role) const
{
	if (!index.isValid())
		return QVariant();
	dbheader *hd = allHeaders[index.column()];
	pki_base *item = static_cast<pki_base*>(index.internalPointer());
	switch (role) {
		case Qt::EditRole:
		case Qt::DisplayRole:
			return item->column_data(hd);
		case Qt::DecorationRole:
			return item->getIcon(hd);
		case Qt::TextAlignmentRole:
			return hd->isNumeric() ? Qt::AlignRight : Qt::AlignLeft;
		case Qt::FontRole:
			return QVariant(XCA_application::tableFont);
		case Qt::BackgroundRole:
			return item->bg_color(hd);
		case Qt::UserRole:
			return item->visible();
	}
	return QVariant();
}
static QVariant getHeaderViewInfo(int sect, dbheader *h)
{
	return QVariant(
#if 0
	QString("H[%1] Show:%2%3 Size:%4 VI:%5 Indi:%6").
		arg(sect).arg(h->show).arg(h->showDefault).arg(h->size).
		arg(h->visualIndex).arg(h->sortIndicator)
#else
	h->getTooltip()
#endif
	);
}

QVariant db_base::headerData(int section, Qt::Orientation orientation,
		int role) const
{
	if (orientation == Qt::Horizontal) {
		switch (role) {
		case Qt::DisplayRole:
			return QVariant(allHeaders[section]->getName());
		case Qt::ToolTipRole:
			return getHeaderViewInfo(section, allHeaders[section]);
		}
	}
	return QVariant();
}

Qt::ItemFlags db_base::flags(const QModelIndex &index) const
{
	if (!index.isValid())
		return Qt::ItemIsEnabled;

	if (index.column() == 0)
		return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
	else
		return QAbstractItemModel::flags(index);
}

bool db_base::setData(const QModelIndex &index, const QVariant &value, int role)
{
	QString on, nn;
	pki_base *item;
	if (index.isValid() && role == Qt::EditRole) {
		nn = value.toString();
		item = static_cast<pki_base*>(index.internalPointer());
		on = item->getIntName();
		if (nn == on)
			return true;
		db mydb(dbName);
		try {
			mydb.rename(item->getType(), on, nn);
			item->setIntName(nn);
			emit dataChanged(index, index);
			return true;
		} catch (errorEx &err) {
			mainwin->Error(err);
		}
	}
	return false;
}

void db_base::load_default(load_base &load)
{
	QString s;
	QStringList slist = QFileDialog::getOpenFileNames(mainwin, load.caption,
			mainwin->getPath(), load.filter);

	if (!slist.count())
		return;

	QString fn = slist[0];
	mainwin->setPath(fn.mid(0, fn.lastIndexOf("/")));

	ImportMulti *dlgi = new ImportMulti(mainwin);
	foreach(s, slist) {
		pki_base *item = NULL;
		try {
			item = load.loadItem(s);
			dlgi->addItem(item);
		}
		catch (errorEx &err) {
			MainWindow::Error(err);
			delete item;
		}
	}
	dlgi->execute();
	delete dlgi;
}

void db_base::edit()
{
	if (!currentIdx.isValid())
		return;
	emit editItem(currentIdx);
}

void db_base::showItem()
{
	if (!currentIdx.isValid())
		return;
	showItem(currentIdx);
}

bool db_base::columnHidden(int col) const
{
	return !allHeaders[col]->show;
}

void db_base::columnResetDefaults()
{
	dbheader *hd;
	foreach(hd, allHeaders) {
		hd->reset();
	}
	emit resetHeader();
}

bool db_base::isNumericCol(int col) const
{
	return allHeaders[col]->isNumeric();
}

void db_base::showHeaderMenu(QContextMenuEvent *e, int sect)
{
	contextMenu(e, NULL, sect);
}

void db_base::contextMenu(QContextMenuEvent *e, QMenu *parent, int)
{
	int shown = 0;
	tipMenu *menu, *dn, *v3ext, *current, *v3ns;
	QAction *a, *sep;
	dbheader *hd;

	menu = new tipMenu(QString(), mainwin);
	dn = new tipMenu(tr("Subject entries"), mainwin);
	v3ext = new tipMenu(tr("X509v3 Extensions"), mainwin);
	v3ns = new tipMenu(tr("Netscape extensions"), mainwin);
	menu->addAction(tr("Reset"), this, SLOT(columnResetDefaults()));
	sep = menu->addSeparator();
	foreach(hd, allHeaders) {
		switch (hd->type) {
			case dbheader::hd_x509name:
				current = dn;
				break;
			case dbheader::hd_v3ext:
				current = v3ext;
				break;
			case dbheader::hd_v3ext_ns:
				current = v3ns;
				break;
			default:
				current = menu;
				break;
		}
		a = current->addAction(hd->getName());
		a->setCheckable(true);
		a->setChecked(hd->show);
		a->setToolTip(hd->getTooltip());
		hd->action = a;
	}
	if (!dn->isEmpty() || !v3ext->isEmpty())
		menu->insertSeparator(sep);

	if (!dn->isEmpty())
		menu->insertMenu(sep, dn);
	else
		delete dn;

	if (!v3ext->isEmpty()) {
		if (!v3ns->isEmpty()) {
			v3ext->addSeparator();
			v3ext->addMenu(v3ns);
		} else {
			delete v3ns;
		}
		menu->insertMenu(sep, v3ext);
	} else {
		delete v3ext;
		delete v3ns;
	}
	if (parent) {
		parent->addAction(tr("Paste PEM data"), mainwin,
				SLOT(pastePem()));
		parent->addMenu(menu)->setText(tr("Columns"));
		parent->exec(e->globalPos());
	} else {
		menu->exec(e->globalPos());
	}
	foreach(hd, allHeaders) {
		if (hd->action)
			hd->show = hd->action->isChecked();
		shown += hd->show ? 1 : 0;
		hd->action = NULL;
	}
	if (!shown)
		allHeaders[0]->show = true;
        delete menu;
	if (parent)
		delete parent;
	emit updateHeader();
}
