/* vi: set sw=4 ts=4:
 *
 * Copyright (C) 2006 - 2015 Christian Hohnstaedt.
 *
 * All rights reserved.
 */


#include "XcaTreeView.h"
#include "lib/db_base.h"
#include <QtCore/QAbstractItemModel>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QHeaderView>
#include <QtGui/QContextMenuEvent>
#include <QtCore/QVariant>
#include <QtCore/QRegExp>


XcaTreeView::XcaTreeView(QWidget *parent)
	:QTreeView(parent)
{
	setHeader(new XcaHeaderView());
	setAlternatingRowColors(true);
	setSelectionMode(QAbstractItemView::ExtendedSelection);
	setEditTriggers(QAbstractItemView::EditKeyPressed);
	setRootIsDecorated(false);
	setUniformRowHeights(true);
	//setAnimated(true);

	proxy = new XcaProxyModel(this);
	setSortingEnabled(true);
	proxy->setDynamicSortFilter(true);
	sortByColumn(0, Qt::AscendingOrder);
	basemodel = NULL;
	connect(header(), SIGNAL(sectionHandleDoubleClicked(int)),
		this, SLOT(resizeColumnToContents(int)));
	header()->setSectionsClickable(true);
}

XcaTreeView::~XcaTreeView()
{
	delete proxy;
}

void XcaTreeView::contextMenuEvent(QContextMenuEvent * e)
{
	if (!basemodel)
		return;
	basemodel->showContextMenu(e, getIndex(indexAt(e->pos())));
}

void XcaTreeView::showHideSections()
{
	if (!basemodel)
		return;
	int i, max = basemodel->columnCount(QModelIndex());
	basemodel->colResizeStart();
	for (i=0; i<max; i++) {
		if (basemodel->columnHidden(i))
			header()->hideSection(i);
		else
			header()->showSection(i);
	}
	basemodel->colResizeEnd();
	columnsResize();
}

void XcaTreeView::setModel(QAbstractItemModel *model)
{
	QByteArray ba;

	basemodel = (db_base *)model;

	proxy->setSourceModel(model);
	QTreeView::setModel(proxy);

	if (basemodel) {
		connect(basemodel, SIGNAL(resetHeader()),
			header(), SLOT(resetMoves()));
		connect(basemodel, SIGNAL(resetHeader()),
			this, SLOT(columnsResize()));
		connect(basemodel, SIGNAL(updateHeader()),
			this, SLOT(showHideSections()));
		connect(header(), SIGNAL(sectionMoved(int,int,int)),
			this, SLOT(sectionMoved(int,int,int)));
		connect(header(), SIGNAL(sectionResized(int,int,int)),
			basemodel, SLOT(sectionResized(int,int,int)));
		connect(header(), SIGNAL(sortIndicatorChanged(int,Qt::SortOrder)),
			basemodel, SLOT(sortIndicatorChanged(int,Qt::SortOrder)));
		connect(basemodel, SIGNAL(columnsContentChanged()),
			this, SLOT(columnsResize()));
		connect(basemodel, SIGNAL(columnsContentChanged()),
			proxy, SLOT(invalidate()));
		connect(basemodel, SIGNAL(editItem(const QModelIndex &)),
			this, SLOT(editIdx(const QModelIndex &)));

		basemodel->initHeaderView(header());
	}
	showHideSections();
}

void XcaTreeView::headerEvent(QContextMenuEvent *e, int col)
{
	basemodel->showHeaderMenu(e, col);
}

QModelIndex XcaTreeView::getIndex(const QModelIndex &index)
{
	return proxy->mapToSource(index);
}

QModelIndex XcaTreeView::getProxyIndex(const QModelIndex &index)
{
	return proxy->mapFromSource(index);
}

QModelIndexList XcaTreeView::getSelectedIndexes()
{
	QItemSelection indexes = selectionModel()->selection();
	return proxy->mapSelectionToSource(indexes).indexes();
}

void XcaTreeView::columnsResize()
{
	int cnt, i;
	if (!basemodel)
		return;
	cnt = basemodel->columnCount(QModelIndex());
	basemodel->colResizeStart();
	for (i=0; i<cnt; i++) {
		if (!basemodel->fixedHeaderSize(i)) {
			resizeColumnToContents(i);
		}
	}
	basemodel->colResizeEnd();
}

void XcaTreeView::sectionMoved(int, int, int)
{
	int cnt = header()->count();
	for (int i=0; i<cnt; i++) {
		basemodel->setVisualIndex(i, header()->visualIndex(i));
	}
}

void XcaTreeView::editIdx(const QModelIndex &idx)
{
	edit(proxy->mapFromSource(idx));
}

void XcaTreeView::setFilter(const QString &pattern)
{
	 pki_base::limitPattern = QRegExp(pattern,
			Qt::CaseInsensitive, QRegExp::Wildcard);
	// Only to tell the model about the changed filter
	 proxy->setFilterFixedString(pattern);
}

XcaProxyModel::XcaProxyModel(QWidget *parent)
	:QSortFilterProxyModel(parent)
{
}

bool XcaProxyModel::lessThan(const QModelIndex &left,
		const QModelIndex &right) const
{
	db_base *db = (db_base *)sourceModel();
	if (!db)
		return QSortFilterProxyModel::lessThan(left, right);

	if (db->isNumericCol(left.column()) &&
	    db->isNumericCol(right.column()))
	{
		int diff;
		QString l = sourceModel()->data(left).toString();
		QString r = sourceModel()->data(right).toString();
		diff = l.size() - r.size();
		if (diff<0)
			return true;
		else if (diff>0)
			return false;
		else
			return l < r;
	}
	return QSortFilterProxyModel::lessThan(left, right);
}

bool XcaProxyModel::filterAcceptsRow(int sourceRow,
         const QModelIndex &sourceParent) const
{
	QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
	return sourceModel()->data(idx, Qt::UserRole).toBool();
}

QVariant XcaProxyModel::data(const QModelIndex &index, int role) const
{
	QModelIndex i;
	QString number;

	if (index.column() != 1)
		return QSortFilterProxyModel::data(index, role);

	/* Row number */
	switch (role) {
		case Qt::EditRole:
		case Qt::DisplayRole:
			for (i = index; i.isValid(); i = i.parent())
				number += QString(" %1").arg(i.row()+1);
			return QVariant(number);
		default:
			return QSortFilterProxyModel::data(index, role);
	}
	return QVariant();
}

void XcaHeaderView::contextMenuEvent(QContextMenuEvent * e)
{
	XcaTreeView *tv = (XcaTreeView *)parentWidget();
	if (tv)
		tv->headerEvent(e, logicalIndexAt(e->pos()));
}

void XcaHeaderView::resetMoves()
{
	for (int i=0; i<count(); i++) {
		if (i != visualIndex(i)) {
			moveSection(visualIndex(i), i);
			i=0;
		}
	}
	resizeSections(QHeaderView::ResizeToContents);
}
