#include <obs.hpp>
#include <obs-module.h>
#include <util/darray.h>
#include <obs-frontend-api.h>

#include <QWidget>
#include <QDockWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialog>
#include <QAction>
#include <QMainWindow>
#include <QMenu>
#include <QCursor>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QTimer>
#include <QApplication>
#include <QLabel>

class DraggableButton;
class OBSLayerView;
static OBSLayerView *layer_view = nullptr;

static void saveload_callback(obs_data_t *save_data, bool saving, void *vptr);
static void signal_callback_add(void *vptr, calldata_t *cbdata);
static void signal_callback_remove(void *vptr, calldata_t *cbdata);
static bool listSceneItems(obs_scene_t *scene, obs_sceneitem_t *item, void *vptr);
static bool button_drag_callback(QDropEvent *e, DraggableButton *btn);

const char *sceneitem_get_name(obs_sceneitem_t *item)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	return obs_source_get_name(source);
}

const char *sceneitem_get_scene_name(obs_sceneitem_t *item)
{
	obs_scene_t *scene = obs_sceneitem_get_scene(item);
	obs_source_t *source = obs_scene_get_source(scene);
	return obs_source_get_name(source);
}

class DraggableButton : public QPushButton {
private:
	QPoint pressCoord;
	QPoint moveCoord;
	bool(*callback)(QDropEvent *e, DraggableButton *btn) = nullptr;
protected:
	void mousePressEvent(QMouseEvent *e) override
	{
		if (e->button() == Qt::LeftButton) {
			pressCoord = e->globalPos();
			moveCoord = e->globalPos();
		} else {
			pressCoord = {};
			moveCoord = {};
		}
		QPushButton::mousePressEvent(e);
	};

	void mouseMoveEvent(QMouseEvent *e) override
	{
		if (e->button() == Qt::LeftButton)
			moveCoord = e->globalPos();
		else
			moveCoord = {};

		QPoint diff = e->globalPos() - pressCoord;
		if (diff.manhattanLength() >= QApplication::startDragDistance()) {
			QDrag *drag = new QDrag(this);
			QMimeData *mimeData = new QMimeData;
			//mimeData->setText(this->text());
			drag->setMimeData(mimeData);
			//drag->setPixmap(iconPixmap);

			Qt::DropAction dropAction = drag->exec(Qt::MoveAction);
			if (dropAction == Qt::DropAction::IgnoreAction) {
				QPushButton::mouseMoveEvent(e);
			} else {
				e->ignore();
			}
			return;
		}

		QPushButton::mouseMoveEvent(e);
	}

	void mouseReleaseEvent(QMouseEvent *e) override
	{
		QPushButton::mouseReleaseEvent(e);
	}

	void dragEnterEvent(QDragEnterEvent *e) override
	{
		setBackgroundRole(QPalette::Highlight);
		e->acceptProposedAction();
	}

	void dragMoveEvent(QDragMoveEvent *e) override
	{
		e->acceptProposedAction();
	}

	void dropEvent(QDropEvent *e) override
	{
		if (e->source() == this || e->proposedAction() != Qt::MoveAction) {
			e->ignore();
			return;
		}
		if (callback) {
			if (!callback(e, this)) {
				e->ignore();
				return;
			}
		}
		e->acceptProposedAction();
	}
public:
	DraggableButton(const QString &text, QWidget *parent = nullptr)
		: QPushButton(text, parent)
	{
		setAcceptDrops(true);
	};

	~DraggableButton()
	{
	};

	void setCallback(bool(*cb)(QDropEvent *e, DraggableButton *btn) = nullptr)
	{
		blockSignals(true);
		callback = cb;
		blockSignals(false);
	}
};

static const int free_rows = 2;
class OBSLayerView : public QDockWidget {
private:
	QWidget *_customgui = nullptr;
	obs_source_t *_currentSceneSource = nullptr;
	obs_scene_t *_currentScene = nullptr;
	std::vector<obs_sceneitem_t *> _currentItems;

	std::vector<std::vector<obs_sceneitem_t*>> _layers;
	std::vector<std::vector<OBSWeakSource>> _weaklayers;
	
	QFormLayout *_form = nullptr;
	std::vector<size_t> _used;
	QHBoxLayout *toplayout = nullptr;

	obs_data_t *saved = nullptr;
	std::vector<std::vector<std::pair<std::string, std::string>>> _layersinfo;
	bool loaded = false;
protected:
	void dragEnterEvent(QDragEnterEvent *e) override
	{
		setBackgroundRole(QPalette::Highlight);
		e->acceptProposedAction();
	}

	void dragMoveEvent(QDragMoveEvent *e) override
	{
		e->acceptProposedAction();
	}

	void dropEvent(QDropEvent *e) override
	{
		QObject *src = e->source();
		if (!src || src == this || e->proposedAction() != Qt::MoveAction) {
			e->ignore();
			return;
		}
		QVariant scene_item = src->property("scene_item");
		if (!scene_item.isValid()) {
			blog(LOG_WARNING, "missing scene item");
			e->ignore();
			return;
		}
		QTimer::singleShot(50, [=]() {
			if(layer_view)
				layer_view->AddItemToLayer((obs_sceneitem_t*)scene_item.toULongLong(),
					_layers.size());
		});
		e->acceptProposedAction();
	}
public:
	void CompleteLoad(std::vector<obs_sceneitem_t*> items)
	{
		for (obs_sceneitem_t *item : items) {
			std::string name = sceneitem_get_name(item);
			std::string sname = sceneitem_get_scene_name(item);
			size_t index = ItemInLayer(item);
			if (name.empty() || index < _layers.size())
				continue;
			for (size_t i = 0; i < _layersinfo.size(); i++) {
				std::vector<std::pair<std::string,
					std::string>> pairs = _layersinfo[i];
				for (size_t j = 0; j < pairs.size(); j++) {
					std::string n = pairs[j].first;
					std::string s = pairs[j].second;
					if (name == n && sname == s) {
						_layers.reserve(i + 1);
						while (_layers.size() < (i+1))
							_layers.push_back({});
						_layers[i].push_back(item);
						break;
					}
				}
			}
		}
	}

	void LoadData(obs_data_t *save_data)
	{
		obs_data_array_t *layers = obs_data_get_array(save_data, "layers");
		size_t count = obs_data_array_count(layers);
		_layersinfo.clear();
		_layersinfo.reserve(count);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *layeritem = obs_data_array_item(layers, i);
			obs_data_array_t *layer = obs_data_get_array(layeritem, "layer");
			size_t count2 = obs_data_array_count(layer);
			std::vector<std::pair<std::string, std::string>> names;
			names.reserve(count2);
			for (size_t j = 0; j < count; j++) {
				obs_data_t *itemdata = obs_data_array_item(layer, j);
				std::string name = obs_data_get_string(itemdata, "name");
				std::string sname = obs_data_get_string(itemdata, "scene");
				if (!name.empty() && !sname.empty()) {
					std::pair<std::string, std::string> element
						= { name, sname };
					auto it = std::find(names.begin(),
						names.end(), element);
					if (it == names.end())
						names.push_back(element);
				}
				obs_data_release(itemdata);
			}
			_layersinfo.push_back(names);
			obs_data_array_release(layer);
			obs_data_release(layeritem);
		}
		obs_data_array_release(layers);
	}

	void SaveData(obs_data_t *save_data)
	{
		obs_data_array_t *layerarray = obs_data_array_create();
		for (size_t i = 0; i < _layers.size(); i++) {
			std::vector<obs_sceneitem_t*> layer = _layers[i];
			if (!layer.size())
				continue;
			obs_data_t *layerdata = obs_data_create();
			obs_data_array_t *itemarray = obs_data_array_create();
			for (obs_sceneitem_t *item : layer) {
				obs_data_t *iteminfo = obs_data_create();
				obs_source_t *source = obs_sceneitem_get_source(item);
				obs_scene_t *scene = obs_sceneitem_get_scene(item);
				obs_source_t *scene_source = obs_scene_get_source(scene);
				const char *name = obs_source_get_name(source);
				const char *sname = obs_source_get_name(scene_source);
				obs_data_set_string(iteminfo, "name", name);
				obs_data_set_string(iteminfo, "scene", sname);
				obs_data_array_push_back(itemarray, iteminfo);
				obs_data_release(iteminfo);
			}

			obs_data_set_array(layerdata, "layer", itemarray);
			obs_data_array_push_back(layerarray, layerdata);
			obs_data_array_release(itemarray);
			obs_data_release(layerdata);
		}
		obs_data_set_array(save_data, "layers", layerarray);
		obs_data_array_release(layerarray);
	}

	void AddSceneItem(OBSSceneItem item)
	{
		obs_sceneitem_t *itm = item;
		obs_source_t *source = obs_sceneitem_get_source(item);
		const char *name = obs_source_get_name(source);
		if (toplayout) {
			QString qname(name);
			QPushButton *button = new QPushButton(qname);
			QVariant val((uint64_t)itm);
			button->setProperty("scene_item", val);
			connect(button, &QPushButton::clicked, this, &OBSLayerView::MenuDropDown);
			/*Connect Button*/
			toplayout->addWidget(button);
			update();
			//repaint();
		}
	}

	void RemoveSceneItem(OBSSceneItem item)
	{
		obs_sceneitem_t *test = item;
		if (toplayout) {
			for (int i = 0; i < toplayout->count(); i++) {
				QWidget *button = toplayout->itemAt(i)->widget();
				QVariant val = button->property("scene_item");
				obs_sceneitem_t *buttonitem = (obs_sceneitem_t*)val.toULongLong();
				if (buttonitem == test) {
					delete button;
					i--;
				}
			}
		}
		RemoveFromAllLayers(item);

		update();
		//repaint();
	}

	void RemoveFromAllLayers(OBSSceneItem item)
	{
		obs_sceneitem_t *test = item;
		for (size_t i = 0; i < _layers.size(); i++) {
			_layers[i].erase(std::remove_if(_layers[i].begin(), _layers[i].end(), [=](obs_sceneitem_t *it) {
				return item == it;
			}), _layers[i].end());

			for (int k = free_rows; k < _form->count(); k++) {
				QLayoutItem *layoutitem = _form->itemAt(k);
				if (!layoutitem)
					continue;
				QHBoxLayout *layerlayout = (QHBoxLayout*)layoutitem->layout();
				if (!layerlayout)
					continue;
				for (int j = 0; j < layerlayout->count(); j++) {
					QWidget *widget = layerlayout->itemAt(j)->widget();
					QVariant val = widget->property("scene_item");
					obs_sceneitem_t *buttonitem = (obs_sceneitem_t*)val.toULongLong();
					if (buttonitem == test) {
						delete widget;
						j--;
					}
				}

				if (layerlayout->count() == 0) {
					QVariant val = layerlayout->property("layer");
					size_t index = val.toULongLong();
					_used.erase(std::remove_if(_used.begin(), _used.end(),
						[=](size_t idx) {
						return idx == index;
					}), _used.end());
					_form->removeRow(k);
					k--;
				}
			}
		}
		/*
		_layers.erase(std::remove_if(_layers.begin(), _layers.end(), [=](std::vector<obs_sceneitem_t*> its) {
			return its.size() == 0;
		}), _layers.end());
		*/
	}

	void AddItemToLayer(obs_sceneitem_t *buttonitem, size_t index)
	{
		RemoveFromAllLayers(buttonitem);
		if (index >= _layers.size()) {
			index = _layers.size();
			_layers.push_back({ buttonitem });

			QHBoxLayout *layerlayout = new QHBoxLayout();
			layerlayout->setProperty("layer", index);
			obs_source_t *source = obs_sceneitem_get_source(buttonitem);
			const char *name = obs_source_get_name(source);
			QString qname(name);
			DraggableButton *button = new DraggableButton(qname);
			QVariant val((uint64_t)buttonitem);
			button->setProperty("scene_item", val);
			button->setProperty("layer", index);
			button->setCallback(button_drag_callback);
			connect(button, &DraggableButton::clicked, this, &OBSLayerView::SwitchViewable);
			/*Connect Button*/
			layerlayout->addWidget(button);
			_form->addRow(layerlayout);
		} else {
			_layers[index].push_back(buttonitem);
			QHBoxLayout *layerlayout = nullptr;
			for (int i = free_rows; i < _form->count(); i++) {
				QLayoutItem *layoutitem = _form->itemAt(i);
				if (!layoutitem)
					continue;
				QHBoxLayout *hbox = (QHBoxLayout*)layoutitem->layout();
				if (!hbox)
					continue;
				QVariant val = hbox->property("layer");
				if (val.toULongLong() == index) {
					layerlayout = hbox;
					break;
				}
			}
			if (!layerlayout) {
				layerlayout = new QHBoxLayout();
				layerlayout->setProperty("layer", index);
				_form->addRow(layerlayout);
			}

			obs_source_t *source = obs_sceneitem_get_source(buttonitem);
			const char *name = obs_source_get_name(source);
			QString qname(name);
			DraggableButton *button = new DraggableButton(qname);
			QVariant val((uint64_t)buttonitem);
			button->setProperty("scene_item", val);
			button->setProperty("layer", index);
			button->setCallback(button_drag_callback);

			connect(button, &DraggableButton::clicked, this, &OBSLayerView::SwitchViewable);
			/*Connect Button*/
			layerlayout->addWidget(button);
		}

		update();
	}

public slots:
	void RemoveFromLayers()
	{
		QObject *action = sender();
		QVariant val = action->property("scene_item");
		obs_sceneitem_t *buttonitem = (obs_sceneitem_t*)val.toULongLong();
		RemoveFromAllLayers(buttonitem);

		update();
		//repaint();
	}

	void AddToLayer()
	{
		QObject *action = sender();
		QVariant val = action->property("scene_item");
		obs_sceneitem_t *buttonitem = (obs_sceneitem_t*)val.toULongLong();
		QVariant layerindex = action->property("layer");
		size_t index = layerindex.toULongLong();

		AddItemToLayer(buttonitem, index);
	}

	void MenuDropDown()
	{
		QObject *btn = sender();
		QVariant val = btn->property("scene_item");
		obs_sceneitem_t *buttonitem = (obs_sceneitem_t*)val.toULongLong();
		size_t index = ItemInLayer(buttonitem);

		QMenu popup;

		for (size_t i = 0; i < _layers.size(); i++) {
			QAction *addToLayer = new QAction(QString::number(i + 1), this);
			addToLayer->setProperty("scene_item", val);
			addToLayer->setProperty("layer", i);
			connect(addToLayer, &QAction::triggered, this, &OBSLayerView::AddToLayer);
			popup.addAction(addToLayer);
		}

		QAction addToNew("+", this);
		addToNew.setProperty("scene_item", val);
		addToNew.setProperty("layer", -1);

		QAction removeItem("-", this);
		removeItem.setProperty("scene_item", val);

		connect(&addToNew, &QAction::triggered, this, &OBSLayerView::AddToLayer);
		connect(&removeItem, &QAction::triggered, this, &OBSLayerView::RemoveFromLayers);
		popup.addAction(&addToNew);
		if (index < _layers.size())
			popup.addAction(&removeItem);
		popup.exec(QCursor::pos());
	}

	void SwitchViewable()
	{
		QObject *btn = sender();
		QVariant val = btn->property("scene_item");
		obs_sceneitem_t *buttonitem = (obs_sceneitem_t*)val.toULongLong();
		obs_source_t *src = obs_sceneitem_get_source(buttonitem);
		const char *n = obs_source_get_name(src);

		size_t index = ItemInLayer(buttonitem);

		QHBoxLayout *layout = nullptr;
		for (int i = free_rows; i < _form->count(); i++) {
			QLayoutItem *layoutitem = _form->itemAt(i);
			if (!layoutitem)
				continue;
			QHBoxLayout *hbox = (QHBoxLayout*)layoutitem->layout();
			if (!hbox)
				continue;
			QVariant val = hbox->property("layer");
			if (val.toULongLong() == index) {
				layout = hbox;
				break;
			}
		}
		
		if (!layout)
			return;
		blog(LOG_DEBUG, "Layer %i: %s", (int)index, n);
		for (int i = 0; i < layout->count(); i++) {
			QWidget *button = layout->itemAt(i)->widget();
			QVariant val = button->property("scene_item");
			obs_sceneitem_t *btnitem = (obs_sceneitem_t*)val.toULongLong();
			obs_sceneitem_set_visible(btnitem, btnitem == buttonitem);
		}
	}
public:
	void closeEvent(QCloseEvent*)
	{
		obs_frontend_save();
	}

	void AddItem(obs_sceneitem_t *item)
	{
		obs_sceneitem_addref(item);
		_currentItems.push_back(item);
	}

	void UseLayer(size_t index)
	{
		for (size_t usedindex : _used) {
			if (index == usedindex)
				return;
		}
		_used.push_back(index);
		std::vector<obs_sceneitem_t*> items = _layers[index];
		QHBoxLayout *layerlayout = new QHBoxLayout();
		layerlayout->setProperty("layer", index);
		for (obs_sceneitem_t *item : items) {
			obs_source_t *source = obs_sceneitem_get_source(item);
			const char *name = obs_source_get_name(source);
			QString qname(name);
			DraggableButton *button = new DraggableButton(qname);
			QVariant val((uint64_t)item);
			button->setProperty("scene_item", val);
			button->setProperty("layer", index);
			button->setCallback(button_drag_callback);

			connect(button, &DraggableButton::clicked, this, &OBSLayerView::SwitchViewable);
			/*Connect Button*/
			layerlayout->addWidget(button);
		}
		_form->addRow(layerlayout);
	}

	size_t ItemInLayer(obs_sceneitem_t *item)
	{
		for (size_t i = 0; i < _layers.size(); i++) {
			for (size_t j = 0; j < _layers[i].size(); j++) {
				if (item == _layers[i][j])
					return i;
			}
		}
		return SIZE_MAX;
	}

	OBSLayerView(QWidget *parent = nullptr, Qt::WindowFlags flags = Qt::WindowFlags())
		: QDockWidget(parent, flags)
	{
		_customgui = new QWidget();
		setWidget(_customgui);
		setFeatures(AllDockWidgetFeatures);
		setAllowedAreas(Qt::DockWidgetArea::AllDockWidgetAreas);
		setObjectName("layerView");
		setWindowTitle("Layer View");
		setAcceptDrops(true);

		QMainWindow *app = (QMainWindow*)obs_frontend_get_main_window();
		if (app)
			app->addDockWidget(Qt::BottomDockWidgetArea, this);
		else
			setFloating(true);
	}

	~OBSLayerView()
	{
		for (obs_sceneitem_t *item : _currentItems)
			obs_sceneitem_release(item);

		_currentItems.clear();

		if (_currentSceneSource)
			obs_source_release(_currentSceneSource);

		if (saved)
			obs_data_release(saved);
		layer_view = nullptr;
	}

	void UpdateScene()
	{
		/* Get the current scene we're working w/ */
		if (_currentSceneSource)
			obs_source_release(_currentSceneSource);
		_currentSceneSource = obs_frontend_get_current_scene();
		_currentScene = obs_scene_from_source(_currentSceneSource);
		for (obs_sceneitem_t *item : _currentItems)
			obs_sceneitem_release(item);

		_currentItems.clear();
		obs_scene_enum_items(_currentScene, listSceneItems, nullptr);

		CompleteLoad(_currentItems);

		/* Update GUI */
		if (!_form) {
			_form = new QFormLayout(this);
			_customgui->setLayout(_form);
		}
		while (_form->rowCount() > 0)
			_form->removeRow(0);
		_used.clear();
		_used.reserve(_layers.size());

		toplayout = new QHBoxLayout();
		//QLabel *layers_label = new QLabel("Layers");
		//layers_label->setAlignment(Qt::AlignHCenter);

		for (obs_sceneitem_t *item : _currentItems) {
			obs_source_t *source = obs_sceneitem_get_source(item);
			const char *name = obs_source_get_name(source);
			QString qname(name);
			DraggableButton *button = new DraggableButton(qname);
			connect(button, &DraggableButton::clicked, this, &OBSLayerView::MenuDropDown);

			QVariant val((uint64_t)item);
			button->setProperty("scene_item", val);
			button->setVisible(true);
			toplayout->addWidget(button);
		}
		_form->addRow(toplayout);
		_form->addRow(new QWidget());

		for (obs_sceneitem_t *item : _currentItems) {
			size_t index = ItemInLayer(item);
			if (index < _layers.size())
				UseLayer(index);
		}

		update();
		//repaint();
	}
};
