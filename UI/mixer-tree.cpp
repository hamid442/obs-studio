#include "window-basic-main.hpp"
#include "obs-app.hpp"
#include "mixer-tree.hpp"
#include "qt-wrappers.hpp"
#include "visibility-checkbox.hpp"
#include "locked-checkbox.hpp"
#include "expand-checkbox.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <string>

#include <QLabel>
#include <QLineEdit>
#include <QSpacerItem>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>

#include <QStylePainter>
#include <QStyleOptionFocusRect>

static inline OBSScene GetCurrentScene()
{
	OBSBasic *main = reinterpret_cast<OBSBasic*>(App()->GetMainWindow());
	return main->GetCurrentScene();
}

/* ========================================================================= */

void MixerTreeModel::OBSFrontendEvent(enum obs_frontend_event event, void *ptr)
{
	MixerTreeModel *stm = reinterpret_cast<MixerTreeModel *>(ptr);

	switch ((int)event) {
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
		stm->SceneChanged();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		stm->Clear();
		break;
	}
}

void MixerTreeModel::Clear()
{
	beginResetModel();
	items.clear();
	endResetModel();
}

static bool enumItem(obs_scene_t*, obs_sceneitem_t *item, void *ptr)
{
	QVector<OBSSceneItem> &items =
		*reinterpret_cast<QVector<OBSSceneItem>*>(ptr);

	obs_source_t *source = obs_sceneitem_get_source(item);
	uint32_t out_flags = obs_source_get_output_flags(source);

	if(out_flags & OBS_SOURCE_AUDIO)
		items.insert(0, item);
	return true;
}

void MixerTreeModel::SceneChanged()
{
	OBSScene scene = GetCurrentScene();

	beginResetModel();
	items.clear();
	obs_scene_enum_items(scene, enumItem, &items);
	endResetModel();

	st->ResetWidgets();

	for (int i = 0; i < items.count(); i++) {
		bool select = obs_sceneitem_selected(items[i]);
		QModelIndex index = createIndex(i, 0);

		st->selectionModel()->select(index, select
				? QItemSelectionModel::Select
				: QItemSelectionModel::Deselect);
	}
}

/* moves a scene item index (blame linux distros for using older Qt builds) */
static inline void MoveItem(QVector<OBSSceneItem> &items, int oldIdx, int newIdx)
{
	OBSSceneItem item = items[oldIdx];
	items.remove(oldIdx);
	items.insert(newIdx, item);
}

/* reorders list optimally with model reorder funcs */
void MixerTreeModel::ReorderItems()
{
	OBSScene scene = GetCurrentScene();

	QVector<OBSSceneItem> newitems;
	obs_scene_enum_items(scene, enumItem, &newitems);

	/* if item list has changed size, do full reset */
	if (newitems.count() != items.count()) {
		SceneChanged();
		return;
	}

	for (;;) {
		int idx1Old = 0;
		int idx1New = 0;
		int count;
		int i;

		/* find first starting changed item index */
		for (i = 0; i < newitems.count(); i++) {
			obs_sceneitem_t *oldItem = items[i];
			obs_sceneitem_t *newItem = newitems[i];
			if (oldItem != newItem) {
				idx1Old = i;
				break;
			}
		}

		/* if everything is the same, break */
		if (i == newitems.count()) {
			break;
		}

		/* find new starting index */
		for (i = idx1Old + 1; i < newitems.count(); i++) {
			obs_sceneitem_t *oldItem = items[idx1Old];
			obs_sceneitem_t *newItem = newitems[i];

			if (oldItem == newItem) {
				idx1New = i;
				break;
			}
		}

		/* if item could not be found, do full reset */
		if (i == newitems.count()) {
			SceneChanged();
			return;
		}

		/* get move count */
		for (count = 1; (idx1New + count) < newitems.count(); count++) {
			int oldIdx = idx1Old + count;
			int newIdx = idx1New + count;

			obs_sceneitem_t *oldItem = items[oldIdx];
			obs_sceneitem_t *newItem = newitems[newIdx];

			if (oldItem != newItem) {
				break;
			}
		}

		/* move items */
		beginMoveRows(QModelIndex(), idx1Old, idx1Old + count - 1,
		              QModelIndex(), idx1New + count);
		for (i = 0; i < count; i++) {
			int to = idx1New + count;
			if (to > idx1Old)
				to--;
			MoveItem(items, idx1Old, to);
		}
		endMoveRows();
	}
}

void MixerTreeModel::Add(obs_sceneitem_t *item)
{
	beginInsertRows(QModelIndex(), 0, 0);
	items.insert(0, item);
	endInsertRows();

	st->UpdateWidget(createIndex(0, 0, nullptr), item);
}

void MixerTreeModel::Remove(obs_sceneitem_t *item)
{
	int idx = -1;
	for (int i = 0; i < items.count(); i++) {
		if (items[i] == item) {
			idx = i;
			break;
		}
	}

	if (idx == -1)
		return;

	int startIdx = idx;
	int endIdx = idx;

	beginRemoveRows(QModelIndex(), startIdx, endIdx);
	items.remove(idx, endIdx - startIdx + 1);
	endRemoveRows();
}

OBSSceneItem MixerTreeModel::Get(int idx)
{
	if (idx == -1 || idx >= items.count())
		return OBSSceneItem();
	return items[idx];
}

MixerTreeModel::MixerTreeModel(MixerTree *st_)
	: QAbstractListModel (st_),
	  st                 (st_)
{
	obs_frontend_add_event_callback(OBSFrontendEvent, this);
}

MixerTreeModel::~MixerTreeModel()
{
	obs_frontend_remove_event_callback(OBSFrontendEvent, this);
}

int MixerTreeModel::rowCount(const QModelIndex &parent) const
{
	return parent.isValid() ? 0 : items.count();
}

QVariant MixerTreeModel::data(const QModelIndex &index, int role) const
{
	if (role == Qt::AccessibleTextRole) {
		OBSSceneItem item = items[index.row()];
		obs_source_t *source = obs_sceneitem_get_source(item);
		return QVariant(QT_UTF8(obs_source_get_name(source)));
	}

	return QVariant();
}

Qt::ItemFlags MixerTreeModel::flags(const QModelIndex &index) const
{
	if (!index.isValid())
		return QAbstractListModel::flags(index) | Qt::ItemIsDropEnabled;

	obs_sceneitem_t *item = items[index.row()];

	return QAbstractListModel::flags(index) |
	       Qt::ItemIsEditable |
	       Qt::ItemIsDragEnabled;
}

Qt::DropActions MixerTreeModel::supportedDropActions() const
{
	return QAbstractItemModel::supportedDropActions() | Qt::MoveAction;
}

/* ========================================================================= */

MixerTree::MixerTree(QWidget *parent_) : QListView(parent_)
{
	MixerTreeModel *stm_ = new MixerTreeModel(this);
	setModel(stm_);
	setStyleSheet(QString(
		"*[bgColor=\"1\"]{background-color:rgba(255,68,68,33%);}" \
		"*[bgColor=\"2\"]{background-color:rgba(255,255,68,33%);}" \
		"*[bgColor=\"3\"]{background-color:rgba(68,255,68,33%);}" \
		"*[bgColor=\"4\"]{background-color:rgba(68,255,255,33%);}" \
		"*[bgColor=\"5\"]{background-color:rgba(68,68,255,33%);}" \
		"*[bgColor=\"6\"]{background-color:rgba(255,68,255,33%);}" \
		"*[bgColor=\"7\"]{background-color:rgba(68,68,68,33%);}" \
		"*[bgColor=\"8\"]{background-color:rgba(255,255,255,33%);}"));
}

static VolControl *createVolControl(obs_source_t *source)
{
	bool vertical = config_get_bool(GetGlobalConfig(), "BasicWindow",
		"VerticalVolControl");
	VolControl *vol = new VolControl(source, true, vertical);
	/*
	double meterDecayRate = config_get_double(basicConfig, "Audio",
		"MeterDecayRate");
	vol->SetMeterDecayRate(meterDecayRate);

	uint32_t peakMeterTypeIdx = config_get_uint(basicConfig, "Audio",
		"PeakMeterType");

	enum obs_peak_meter_type peakMeterType;
	switch (peakMeterTypeIdx) {
	case 0:
		peakMeterType = SAMPLE_PEAK_METER;
		break;
	case 1:
		peakMeterType = TRUE_PEAK_METER;
		break;
	default:
		peakMeterType = SAMPLE_PEAK_METER;
		break;
	}

	vol->setPeakMeterType(peakMeterType);
	*/
	vol->setContextMenuPolicy(Qt::CustomContextMenu);
	/*
	connect(vol, &QWidget::customContextMenuRequested,
		this, &OBSBasic::VolControlContextMenu);
	connect(vol, &VolControl::ConfigClicked,
		this, &OBSBasic::VolControlContextMenu);
	*/
	return vol;
}

void MixerTree::ResetWidgets()
{
	OBSScene scene = GetCurrentScene();

	MixerTreeModel *stm = GetStm();

	//new MixerTreeItem(this, stm->items[i])
	for (int i = 0; i < stm->items.count(); i++) {
		QModelIndex index = stm->createIndex(i, 0, nullptr);
		OBSSceneItem item = stm->items[i];
		OBSSource source = obs_sceneitem_get_source(item);
		setIndexWidget(index, createVolControl(source));
	}
}

void MixerTree::UpdateWidget(const QModelIndex &idx, obs_sceneitem_t *item)
{
	//new MixerTreeItem(this, item)
	OBSSource source = obs_sceneitem_get_source(item);
	setIndexWidget(idx, createVolControl(source));
}
/*
QSize MixerTree::sizeHintForIndex(const QModelIndex &idx) const
{
	QWidget *w = indexWidget(idx);
	return w->sizeHint();
}
*/
void MixerTree::UpdateWidgets(bool force)
{
	MixerTreeModel *stm = GetStm();

	for (int i = 0; i < stm->items.size(); i++) {
		obs_sceneitem_t *item = stm->items[i];
		MixerTreeItem *widget = GetItemWidget(i);

		if (!widget)
			UpdateWidget(stm->createIndex(i, 0), item);
	}
}

void MixerTree::SelectItem(obs_sceneitem_t *sceneitem, bool select)
{
	MixerTreeModel *stm = GetStm();
	int i = 0;

	for (; i < stm->items.count(); i++) {
		if (stm->items[i] == sceneitem)
			break;
	}

	if (i == stm->items.count())
		return;

	QModelIndex index = stm->createIndex(i, 0);
	if (index.isValid())
		selectionModel()->select(index, select
				? QItemSelectionModel::Select
				: QItemSelectionModel::Deselect);
}

Q_DECLARE_METATYPE(OBSSceneItem);

void MixerTree::mouseDoubleClickEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton)
		QListView::mouseDoubleClickEvent(event);
}

void MixerTree::dropEvent(QDropEvent *event)
{
	if (event->source() != this) {
		QListView::dropEvent(event);
		return;
	}

	OBSScene scene = GetCurrentScene();
	MixerTreeModel *stm = GetStm();
	auto &items = stm->items;
	QModelIndexList indices = selectedIndexes();

	DropIndicatorPosition indicator = dropIndicatorPosition();
	int row = indexAt(event->pos()).row();
	bool emptyDrop = row == -1;

	if (emptyDrop) {
		if (!items.size()) {
			QListView::dropEvent(event);
			return;
		}

		row = items.size() - 1;
		indicator = QAbstractItemView::BelowItem;
	}

	/* --------------------------------------- */
	/* store destination group if moving to a  */
	/* group                                   */

	obs_sceneitem_t *dropItem = items[row]; /* item being dropped on */

	if (indicator == QAbstractItemView::BelowItem ||
	    indicator == QAbstractItemView::OnItem)
		row++;

	if (row < 0 || row > stm->items.count()) {
		QListView::dropEvent(event);
		return;
	}

	/* --------------------------------------- */
	/* build persistent indices                */

	QList<QPersistentModelIndex> persistentIndices;
	persistentIndices.reserve(indices.count());
	for (QModelIndex &index : indices)
		persistentIndices.append(index);
	std::sort(persistentIndices.begin(), persistentIndices.end());

	/* --------------------------------------- */
	/* move all items to destination index     */

	int r = row;
	for (auto &persistentIdx : persistentIndices) {
		int from = persistentIdx.row();
		int to = r;
		int itemTo = to;

		if (itemTo > from)
			itemTo--;

		if (itemTo != from) {
			stm->beginMoveRows(QModelIndex(), from, from,
			                   QModelIndex(), to);
			MoveItem(items, from, itemTo);
			stm->endMoveRows();
		}

		r = persistentIdx.row() + 1;
	}

	std::sort(persistentIndices.begin(), persistentIndices.end());
	int firstIdx = persistentIndices.front().row();
	int lastIdx = persistentIndices.back().row();

	/* --------------------------------------- */
	/* reorder scene items in back-end         */

	QVector<struct obs_sceneitem_order_info> orderList;
	int insertCollapsedIdx = 0;

	/* --------------------------------------- */
	/* update widgets and accept event         */

	UpdateWidgets(true);

	event->accept();
	event->setDropAction(Qt::CopyAction);

	QListView::dropEvent(event);
}

void MixerTree::selectionChanged(
		const QItemSelection &selected,
		const QItemSelection &deselected)
{
	{
		SignalBlocker sourcesSignalBlocker(this);
		MixerTreeModel *stm = GetStm();

		QModelIndexList selectedIdxs = selected.indexes();
		QModelIndexList deselectedIdxs = deselected.indexes();

		for (int i = 0; i < selectedIdxs.count(); i++) {
			int idx = selectedIdxs[i].row();
			obs_sceneitem_select(stm->items[idx], true);
		}

		for (int i = 0; i < deselectedIdxs.count(); i++) {
			int idx = deselectedIdxs[i].row();
			obs_sceneitem_select(stm->items[idx], false);
		}
	}
	QListView::selectionChanged(selected, deselected);
}

void MixerTree::Edit(int row)
{
	return;
	/*
	MixerTreeModel *stm = GetStm();
	if (row < 0 || row >= stm->items.count())
		return;

	QModelIndex index = stm->createIndex(row, 0);
	QWidget *widget = indexWidget(index);
	MixerTreeItem *itemWidget = reinterpret_cast<MixerTreeItem *>(widget);
	if (itemWidget->IsEditing())
		return;

	itemWidget->EnterEditMode();
	edit(index);
	*/
}

bool MixerTree::MultipleBaseSelected() const
{
	MixerTreeModel *stm = GetStm();
	QModelIndexList selectedIndices = selectedIndexes();

	OBSScene scene = GetCurrentScene();

	if (selectedIndices.size() < 1)
		return false;

	for (auto &idx : selectedIndices) {
		obs_sceneitem_t *item = stm->items[idx.row()];

		obs_scene *itemScene = obs_sceneitem_get_scene(item);
		if (itemScene != scene) {
			return false;
		}
	}

	return true;
}

void MixerTree::Remove(OBSSceneItem item)
{
	GetStm()->Remove(item);
	/*
	OBSBasic *main = reinterpret_cast<OBSBasic*>(App()->GetMainWindow());
	main->SaveProject();
	if (!main->SavingDisabled()) {
		obs_scene_t *scene = obs_sceneitem_get_scene(item);
		obs_source_t *sceneSource = obs_scene_get_source(scene);
		obs_source_t *itemSource = obs_sceneitem_get_source(item);
		blog(LOG_INFO, "User Removed source '%s' (%s) from scene '%s'",
				obs_source_get_name(itemSource),
				obs_source_get_id(itemSource),
				obs_source_get_name(sceneSource));
	}
	*/
}
