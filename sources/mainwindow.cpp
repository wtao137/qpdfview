/*

Copyright 2012-2013 Adam Reichold
Copyright 2012 Michał Trybus
Copyright 2012 Alexander Volkov

This file is part of qpdfview.

qpdfview is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

qpdfview is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with qpdfview.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "mainwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPrinter>
#include <QScrollBar>
#include <QShortcut>
#include <QStandardItemModel>
#include <QTableView>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)

#include <QStandardPaths>

#else

#include <QDesktopServices>

#endif // QT_VERSION

#ifdef WITH_SQL

#include <QSqlError>
#include <QSqlQuery>

#endif // WITH_SQL

#include "settings.h"
#include "shortcuthandler.h"
#include "pageitem.h"
#include "documentview.h"
#include "miscellaneous.h"
#include "printdialog.h"
#include "settingsdialog.h"
#include "recentlyusedmenu.h"
#include "bookmarkmenu.h"

Settings* MainWindow::s_settings = 0;

MainWindow::MainWindow(const QString& instanceName, QWidget* parent) : QMainWindow(parent)
{
    setObjectName(instanceName);

    if(s_settings == 0)
    {
        s_settings = Settings::instance();
    }

    s_settings->sync();

    if(s_settings->mainWindow().hasIconTheme())
    {
        QIcon::setThemeName(s_settings->mainWindow().iconTheme());
    }

    if(s_settings->mainWindow().hasStyleSheet())
    {
        qApp->setStyleSheet(s_settings->mainWindow().styleSheet());
    }

    setAcceptDrops(true);

    createWidgets();
    createActions();
    createToolBars();
    createDocks();
    createMenus();

    restoreGeometry(s_settings->mainWindow().geometry());
    restoreState(s_settings->mainWindow().state());

    m_matchCaseCheckBox->setChecked(s_settings->documentView().matchCase());

    createDatabase();

    restoreTabs();
    restoreBookmarks();

    on_tabWidget_currentChanged(m_tabWidget->currentIndex());
}

QSize MainWindow::sizeHint() const
{
    return QSize(600, 800);
}

QMenu* MainWindow::createPopupMenu()
{
    QMenu* menu = new QMenu();

    menu->addAction(m_fileToolBar->toggleViewAction());
    menu->addAction(m_editToolBar->toggleViewAction());
    menu->addAction(m_viewToolBar->toggleViewAction());
    menu->addSeparator();
    menu->addAction(m_outlineDock->toggleViewAction());
    menu->addAction(m_propertiesDock->toggleViewAction());
    menu->addAction(m_thumbnailsDock->toggleViewAction());

    return menu;
}

bool MainWindow::open(const QString& filePath, int page, const QRectF& highlight, bool quiet)
{
    if(m_tabWidget->currentIndex() != -1)
    {
        savePerFileSettings(currentTab());

        if(currentTab()->open(filePath))
        {
            const QFileInfo fileInfo(filePath);

            s_settings->mainWindow().setOpenPath(fileInfo.absolutePath());
            m_recentlyUsedMenu->addOpenAction(filePath);

            m_tabWidget->setTabText(m_tabWidget->currentIndex(), fileInfo.completeBaseName());
            m_tabWidget->setTabToolTip(m_tabWidget->currentIndex(), fileInfo.absoluteFilePath());

            restorePerFileSettings(currentTab());

            currentTab()->jumpToPage(page, false);
            currentTab()->setFocus();

            if(!highlight.isNull())
            {
                currentTab()->temporaryHighlight(page, highlight);
            }

            return true;
        }
        else
        {
            if(!quiet)
            {
                QMessageBox::warning(this, tr("Warning"), tr("Could not open '%1'.").arg(filePath));
            }
        }
    }

    return false;
}

bool MainWindow::openInNewTab(const QString& filePath, int page, const QRectF& highlight, bool quiet)
{
    DocumentView* newTab = new DocumentView(this);

    if(newTab->open(filePath))
    {
        newTab->setContinousMode(s_settings->documentView().continuousMode());
        newTab->setLayoutMode(s_settings->documentView().layoutMode());
        newTab->setScaleMode(s_settings->documentView().scaleMode());
        newTab->setScaleFactor(s_settings->documentView().scaleFactor());
        newTab->setRotation(s_settings->documentView().rotation());
        newTab->setInvertColors(s_settings->documentView().invertColors());
        newTab->setHighlightAll(s_settings->documentView().highlightAll());

        const QFileInfo fileInfo(filePath);

        s_settings->mainWindow().setOpenPath(fileInfo.absolutePath());
        m_recentlyUsedMenu->addOpenAction(filePath);

        int index;

        if(s_settings->mainWindow().newTabNextToCurrentTab())
        {
            index = m_tabWidget->insertTab(m_tabWidget->currentIndex() + 1, newTab, fileInfo.completeBaseName());
        }
        else
        {
            index = m_tabWidget->addTab(newTab, fileInfo.completeBaseName());
        }

        m_tabWidget->setTabToolTip(index, fileInfo.absoluteFilePath());
        m_tabWidget->setCurrentIndex(index);

        QAction* tabAction = new QAction(m_tabWidget->tabText(index), newTab);
        connect(tabAction, SIGNAL(triggered()), SLOT(on_tabAction_triggered()));

        m_tabsMenu->addAction(tabAction);

        on_thumbnails_dockLocationChanged(dockWidgetArea(m_thumbnailsDock));

        connect(newTab, SIGNAL(filePathChanged(QString)), SLOT(on_currentTab_filePathChanged(QString)));
        connect(newTab, SIGNAL(numberOfPagesChanged(int)), SLOT(on_currentTab_numberOfPagesChaned(int)));
        connect(newTab, SIGNAL(currentPageChanged(int)), SLOT(on_currentTab_currentPageChanged(int)));

        connect(newTab, SIGNAL(canJumpChanged(bool,bool)), SLOT(on_currentTab_canJumpChanged(bool,bool)));

        connect(newTab, SIGNAL(continousModeChanged(bool)), SLOT(on_currentTab_continuousModeChanged(bool)));
        connect(newTab, SIGNAL(layoutModeChanged(LayoutMode)), SLOT(on_currentTab_layoutModeChanged(LayoutMode)));
        connect(newTab, SIGNAL(scaleModeChanged(ScaleMode)), SLOT(on_currentTab_scaleModeChanged(ScaleMode)));
        connect(newTab, SIGNAL(scaleFactorChanged(qreal)), SLOT(on_currentTab_scaleFactorChanged(qreal)));
        connect(newTab, SIGNAL(rotationChanged(Rotation)), SLOT(on_currentTab_rotationChanged(Rotation)));

        connect(newTab, SIGNAL(linkClicked(QString,int)), SLOT(on_currentTab_linkClicked(QString,int)));

        connect(newTab, SIGNAL(invertColorsChanged(bool)), SLOT(on_currentTab_invertColorsChanged(bool)));
        connect(newTab, SIGNAL(highlightAllChanged(bool)), SLOT(on_currentTab_highlightAllChanged(bool)));
        connect(newTab, SIGNAL(rubberBandModeChanged(RubberBandMode)), SLOT(on_currentTab_rubberBandModeChanged(RubberBandMode)));

        connect(newTab, SIGNAL(searchFinished()), SLOT(on_currentTab_searchFinished()));
        connect(newTab, SIGNAL(searchProgressChanged(int)), SLOT(on_currentTab_searchProgressChanged(int)));

        connect(newTab, SIGNAL(customContextMenuRequested(QPoint)), SLOT(on_currentTab_customContextMenuRequested(QPoint)));

        connect(newTab->outlineModel(), SIGNAL(modelReset()), SLOT(on_model_reset()));
        connect(newTab->propertiesModel(), SIGNAL(modelReset()), SLOT(on_model_reset()));

        newTab->show();

        restorePerFileSettings(newTab);

        newTab->jumpToPage(page, false);
        newTab->setFocus();

        if(!highlight.isNull())
        {
            newTab->temporaryHighlight(page, highlight);
        }

        return true;
    }
    else
    {
        delete newTab;

        if(!quiet)
        {
            QMessageBox::warning(this, tr("Warning"), tr("Could not open '%1'.").arg(filePath));
        }
    }

    return false;
}

bool MainWindow::jumpToPageOrOpenInNewTab(const QString& filePath, int page, bool refreshBeforeJump, const QRectF& highlight, bool quiet)
{
    const QFileInfo fileInfo(filePath);

    for(int index = 0; index < m_tabWidget->count(); ++index)
    {
        if(QFileInfo(tab(index)->filePath()).absoluteFilePath() == fileInfo.absoluteFilePath())
        {
            m_tabWidget->setCurrentIndex(index);

            if(refreshBeforeJump)
            {
                if(!currentTab()->refresh())
                {
                    return false;
                }
            }

            currentTab()->jumpToPage(page);
            currentTab()->setFocus();

            if(!highlight.isNull())
            {
                currentTab()->temporaryHighlight(page, highlight);
            }

            return true;
        }
    }

    return openInNewTab(filePath, page, highlight, quiet);
}

void MainWindow::startSearch(const QString& text)
{
    if(m_tabWidget->currentIndex() != -1)
    {
        m_searchDock->setVisible(true);

        m_searchLineEdit->setText(text);

        currentTab()->setFocus();

        QTimer::singleShot(0, this, SLOT(on_search_timeout()));
    }
}

void MainWindow::on_tabWidget_currentChanged(int index)
{
    if(index != -1)
    {
        m_refreshAction->setEnabled(true);
        m_saveCopyAction->setEnabled(currentTab()->canSave());
        m_saveAsAction->setEnabled(currentTab()->canSave());
        m_printAction->setEnabled(true);

        m_previousPageAction->setEnabled(true);
        m_nextPageAction->setEnabled(true);
        m_firstPageAction->setEnabled(true);
        m_lastPageAction->setEnabled(true);

        m_jumpToPageAction->setEnabled(true);

        m_searchAction->setEnabled(true);

        m_copyToClipboardModeAction->setEnabled(true);
        m_addAnnotationModeAction->setEnabled(true);

        m_continuousModeAction->setEnabled(true);
        m_twoPagesModeAction->setEnabled(true);
        m_twoPagesWithCoverPageModeAction->setEnabled(true);
        m_multiplePagesModeAction->setEnabled(true);

        m_zoomInAction->setEnabled(true);
        m_zoomOutAction->setEnabled(true);
        m_originalSizeAction->setEnabled(true);
        m_fitToPageWidthModeAction->setEnabled(true);
        m_fitToPageSizeModeAction->setEnabled(true);

        m_rotateLeftAction->setEnabled(true);
        m_rotateRightAction->setEnabled(true);

        m_invertColorsAction->setEnabled(true);

        m_fontsAction->setEnabled(true);

        m_presentationAction->setEnabled(true);

        m_previousTabAction->setEnabled(true);
        m_nextTabAction->setEnabled(true);
        m_closeTabAction->setEnabled(true);
        m_closeAllTabsAction->setEnabled(true);
        m_closeAllTabsButCurrentTabAction->setEnabled(true);

        m_previousBookmarkAction->setEnabled(true);
        m_nextBookmarkAction->setEnabled(true);
        m_addBookmarkAction->setEnabled(true);
        m_removeBookmarkAction->setEnabled(true);

        m_currentPageSpinBox->setEnabled(true);
        m_scaleFactorComboBox->setEnabled(true);
        m_searchLineEdit->setEnabled(true);
        m_matchCaseCheckBox->setEnabled(true);
        m_highlightAllCheckBox->setEnabled(true);

        if(m_searchDock->isVisible())
        {
            m_searchLineEdit->stopTimer();
            m_searchLineEdit->setProgress(currentTab()->searchProgress());
        }

        m_outlineView->setModel(currentTab()->outlineModel());
        m_propertiesView->setModel(currentTab()->propertiesModel());
        m_thumbnailsView->setScene(currentTab()->thumbnailsScene());

        on_model_reset();

        on_currentTab_filePathChanged(currentTab()->filePath());
        on_currentTab_numberOfPagesChaned(currentTab()->numberOfPages());
        on_currentTab_currentPageChanged(currentTab()->currentPage());

        on_currentTab_canJumpChanged(currentTab()->canJumpBackward(), currentTab()->canJumpForward());

        on_currentTab_continuousModeChanged(currentTab()->continousMode());
        on_currentTab_layoutModeChanged(currentTab()->layoutMode());
        on_currentTab_scaleModeChanged(currentTab()->scaleMode());
        on_currentTab_scaleFactorChanged(currentTab()->scaleFactor());
        on_currentTab_rotationChanged(currentTab()->rotation());

        on_currentTab_invertColorsChanged(currentTab()->invertColors());
        on_currentTab_highlightAllChanged(currentTab()->highlightAll());
        on_currentTab_rubberBandModeChanged(currentTab()->rubberBandMode());
    }
    else
    {
        m_refreshAction->setEnabled(false);
        m_saveCopyAction->setEnabled(false);
        m_saveAsAction->setEnabled(false);
        m_printAction->setEnabled(false);

        m_previousPageAction->setEnabled(false);
        m_nextPageAction->setEnabled(false);
        m_firstPageAction->setEnabled(false);
        m_lastPageAction->setEnabled(false);

        m_jumpToPageAction->setEnabled(false);

        m_jumpBackwardAction->setEnabled(false);
        m_jumpForwardAction->setEnabled(false);

        m_searchAction->setEnabled(false);

        m_copyToClipboardModeAction->setEnabled(false);
        m_addAnnotationModeAction->setEnabled(false);

        m_continuousModeAction->setEnabled(false);
        m_twoPagesModeAction->setEnabled(false);
        m_twoPagesWithCoverPageModeAction->setEnabled(false);
        m_multiplePagesModeAction->setEnabled(false);

        m_zoomInAction->setEnabled(false);
        m_zoomOutAction->setEnabled(false);
        m_originalSizeAction->setEnabled(false);
        m_fitToPageWidthModeAction->setEnabled(false);
        m_fitToPageSizeModeAction->setEnabled(false);

        m_rotateLeftAction->setEnabled(false);
        m_rotateRightAction->setEnabled(false);

        m_invertColorsAction->setEnabled(false);

        m_fontsAction->setEnabled(false);

        m_presentationAction->setEnabled(false);

        m_previousTabAction->setEnabled(false);
        m_nextTabAction->setEnabled(false);
        m_closeTabAction->setEnabled(false);
        m_closeAllTabsAction->setEnabled(false);
        m_closeAllTabsButCurrentTabAction->setEnabled(false);

        m_previousBookmarkAction->setEnabled(false);
        m_nextBookmarkAction->setEnabled(false);
        m_addBookmarkAction->setEnabled(false);
        m_removeBookmarkAction->setEnabled(false);

        m_currentPageSpinBox->setEnabled(false);
        m_scaleFactorComboBox->setEnabled(false);
        m_searchLineEdit->setEnabled(false);
        m_matchCaseCheckBox->setEnabled(false);
        m_highlightAllCheckBox->setEnabled(false);

        if(m_searchDock->isVisible())
        {
            m_searchLineEdit->stopTimer();
            m_searchLineEdit->setProgress(0);
        }

        m_searchDock->setVisible(false);

        m_outlineView->setModel(0);
        m_propertiesView->setModel(0);
        m_thumbnailsView->setScene(0);

        setWindowTitle(QLatin1String("qpdfview"));

        m_currentPageSpinBox->setValue(1);
        m_currentPageSpinBox->setSuffix(" / 1");
        m_scaleFactorComboBox->setCurrentIndex(4);

        m_copyToClipboardModeAction->setChecked(false);
        m_addAnnotationModeAction->setChecked(false);

        m_continuousModeAction->setChecked(false);
        m_twoPagesModeAction->setChecked(false);
        m_twoPagesWithCoverPageModeAction->setChecked(false);
        m_multiplePagesModeAction->setChecked(false);

        m_fitToPageSizeModeAction->setChecked(false);
        m_fitToPageWidthModeAction->setChecked(false);

        m_invertColorsAction->setChecked(false);
    }
}

void MainWindow::on_tabWidget_tabCloseRequested(int index)
{
    savePerFileSettings(tab(index));

    delete m_tabWidget->widget(index);
}

void MainWindow::on_currentTab_filePathChanged(const QString& filePath)
{
    for(int index = 0; index < m_tabWidget->count(); ++index)
    {
        if(sender() == m_tabWidget->widget(index))
        {
            const QFileInfo fileInfo(filePath);

            m_tabWidget->setTabText(index, fileInfo.completeBaseName());
            m_tabWidget->setTabToolTip(index, fileInfo.absoluteFilePath());

            foreach(QAction* tabAction, m_tabsMenu->actions())
            {
                if(tabAction->parent() == m_tabWidget->widget(index))
                {
                    tabAction->setText(m_tabWidget->tabText(index));

                    break;
                }
            }

            break;
        }
    }

    if(senderIsCurrentTab())
    {
        setWindowTitle(m_tabWidget->tabText(m_tabWidget->currentIndex()) + windowTitleSuffixForCurrentTab());
    }
}

void MainWindow::on_currentTab_numberOfPagesChaned(int numberOfPages)
{
    if(senderIsCurrentTab())
    {
        m_currentPageSpinBox->setRange(1, numberOfPages);
        m_currentPageSpinBox->setSuffix(QString(" / %1").arg(numberOfPages));

        setWindowTitle(m_tabWidget->tabText(m_tabWidget->currentIndex()) + windowTitleSuffixForCurrentTab());
    }
}

static bool synchronizeOutlineView(TreeView* outlineView, const QAbstractItemModel* model, const QModelIndex& parent, int currentPage)
{
    for(int row = 0; row < model->rowCount(parent); ++row)
    {
        const QModelIndex index = model->index(row, 0, parent);

        bool ok = false;
        const int page = model->data(index, Qt::UserRole + 1).toInt(&ok);

        if(ok && page == currentPage)
        {
            outlineView->setCurrentIndex(index);
            return true;
        }
    }

    for(int row = 0; row < model->rowCount(parent); ++row)
    {
        QModelIndex index = model->index(row, 0, parent);

        if(synchronizeOutlineView(outlineView, model, index, currentPage))
        {
            outlineView->expand(index);
            return true;
        }
    }

    return false;
}

void MainWindow::on_currentTab_currentPageChanged(int currentPage)
{
    if(senderIsCurrentTab())
    {
        m_currentPageSpinBox->setValue(currentPage);

        setWindowTitle(m_tabWidget->tabText(m_tabWidget->currentIndex()) + windowTitleSuffixForCurrentTab());

        if(s_settings->mainWindow().synchronizeOutlineView() && m_outlineView->model() != 0)
        {
            synchronizeOutlineView(m_outlineView, m_outlineView->model(), QModelIndex(), currentPage);
        }

        m_thumbnailsView->ensureVisible(currentTab()->thumbnailItems().at(currentPage - 1));
    }
}

void MainWindow::on_currentTab_canJumpChanged(bool backward, bool forward)
{
    if(senderIsCurrentTab())
    {
        m_jumpBackwardAction->setEnabled(backward);
        m_jumpForwardAction->setEnabled(forward);
    }
}

void MainWindow::on_currentTab_continuousModeChanged(bool continuousMode)
{
    if(senderIsCurrentTab())
    {
        m_continuousModeAction->setChecked(continuousMode);

        s_settings->documentView().setContinuousMode(continuousMode);
    }
}

void MainWindow::on_currentTab_layoutModeChanged(LayoutMode layoutMode)
{
    if(senderIsCurrentTab())
    {
        m_twoPagesModeAction->setChecked(layoutMode == TwoPagesMode);
        m_twoPagesWithCoverPageModeAction->setChecked(layoutMode == TwoPagesWithCoverPageMode);
        m_multiplePagesModeAction->setChecked(layoutMode == MultiplePagesMode);

        s_settings->documentView().setLayoutMode(layoutMode);
    }
}

void MainWindow::on_currentTab_scaleModeChanged(ScaleMode scaleMode)
{
    if(senderIsCurrentTab())
    {
        switch(scaleMode)
        {
        default:
        case ScaleFactorMode:
            m_fitToPageWidthModeAction->setChecked(false);
            m_fitToPageSizeModeAction->setChecked(false);

            on_currentTab_scaleFactorChanged(currentTab()->scaleFactor());
            break;
        case FitToPageWidthMode:
            m_fitToPageWidthModeAction->setChecked(true);
            m_fitToPageSizeModeAction->setChecked(false);

            m_scaleFactorComboBox->setCurrentIndex(0);

            m_zoomInAction->setEnabled(true);
            m_zoomOutAction->setEnabled(true);
            break;
        case FitToPageSizeMode:
            m_fitToPageWidthModeAction->setChecked(false);
            m_fitToPageSizeModeAction->setChecked(true);

            m_scaleFactorComboBox->setCurrentIndex(1);

            m_zoomInAction->setEnabled(true);
            m_zoomOutAction->setEnabled(true);
            break;
        }

        s_settings->documentView().setScaleMode(scaleMode);
    }
}

void MainWindow::on_currentTab_scaleFactorChanged(qreal scaleFactor)
{
    if(senderIsCurrentTab())
    {
        if(currentTab()->scaleMode() == ScaleFactorMode)
        {
            m_scaleFactorComboBox->setCurrentIndex(m_scaleFactorComboBox->findData(scaleFactor));
            m_scaleFactorComboBox->lineEdit()->setText(QString("%1 %").arg(qRound(scaleFactor * 100.0)));

            m_zoomInAction->setDisabled(qFuzzyCompare(scaleFactor, Defaults::DocumentView::maximumScaleFactor()));
            m_zoomOutAction->setDisabled(qFuzzyCompare(scaleFactor, Defaults::DocumentView::minimumScaleFactor()));
        }

        s_settings->documentView().setScaleFactor(scaleFactor);
    }
}

void MainWindow::on_currentTab_rotationChanged(Rotation rotation)
{
    if(senderIsCurrentTab())
    {
        s_settings->documentView().setRotation(rotation);
    }
}

void MainWindow::on_currentTab_linkClicked(const QString& filePath, int page)
{
    jumpToPageOrOpenInNewTab(filePath, page, true);
}

void MainWindow::on_currentTab_invertColorsChanged(bool invertColors)
{
    if(senderIsCurrentTab())
    {
        m_invertColorsAction->setChecked(invertColors);

        s_settings->documentView().setInvertColors(invertColors);
    }
}

void MainWindow::on_currentTab_highlightAllChanged(bool highlightAll)
{
    if(senderIsCurrentTab())
    {
        m_highlightAllCheckBox->setChecked(highlightAll);

        s_settings->documentView().setHighlightAll(highlightAll);
    }
}

void MainWindow::on_currentTab_rubberBandModeChanged(RubberBandMode rubberBandMode)
{
    if(senderIsCurrentTab())
    {
        m_copyToClipboardModeAction->setChecked(rubberBandMode == CopyToClipboardMode);
        m_addAnnotationModeAction->setChecked(rubberBandMode == AddAnnotationMode);
    }
}

void MainWindow::on_currentTab_searchFinished()
{
    if(senderIsCurrentTab())
    {
        m_searchLineEdit->setProgress(0);
    }
}

void MainWindow::on_currentTab_searchProgressChanged(int progress)
{
    if(senderIsCurrentTab())
    {
        m_searchLineEdit->setProgress(progress);
    }
}

void MainWindow::on_currentTab_customContextMenuRequested(const QPoint& pos)
{
    if(senderIsCurrentTab())
    {
        QMenu menu;

        menu.addSeparator();
        menu.addActions(QList< QAction* >() << m_previousPageAction << m_nextPageAction << m_firstPageAction << m_lastPageAction << m_jumpToPageAction);

        menu.addSeparator();
        menu.addActions(QList< QAction* >() << m_jumpBackwardAction << m_jumpForwardAction);

        if(m_searchDock->isVisible())
        {
            menu.addSeparator();
            menu.addActions(QList< QAction* >() << m_findPreviousAction << m_findNextAction << m_cancelSearchAction);
        }

        menu.exec(currentTab()->mapToGlobal(pos));
    }
}

void MainWindow::on_currentPage_editingFinished()
{
    if(m_tabWidget->currentIndex() != -1)
    {
        currentTab()->jumpToPage(m_currentPageSpinBox->value());
    }
}

void MainWindow::on_currentPage_returnPressed()
{
    currentTab()->setFocus();
}

void MainWindow::on_scaleFactor_activated(int index)
{
    if(index == 0)
    {
        currentTab()->setScaleMode(FitToPageWidthMode);
    }
    else if(index == 1)
    {
        currentTab()->setScaleMode(FitToPageSizeMode);
    }
    else
    {
        bool ok = false;
        const qreal scaleFactor = m_scaleFactorComboBox->itemData(index).toReal(&ok);

        if(ok)
        {
            currentTab()->setScaleFactor(scaleFactor);
            currentTab()->setScaleMode(ScaleFactorMode);
        }
    }

    currentTab()->setFocus();
}

void MainWindow::on_scaleFactor_editingFinished()
{
    if(m_tabWidget->currentIndex() != -1)
    {
        bool ok = false;
        qreal scaleFactor = m_scaleFactorComboBox->lineEdit()->text().toInt(&ok) / 100.0;

        scaleFactor = scaleFactor >= Defaults::DocumentView::minimumScaleFactor() ? scaleFactor : Defaults::DocumentView::minimumScaleFactor();
        scaleFactor = scaleFactor <= Defaults::DocumentView::maximumScaleFactor() ? scaleFactor : Defaults::DocumentView::maximumScaleFactor();

        if(ok)
        {
            currentTab()->setScaleFactor(scaleFactor);
            currentTab()->setScaleMode(ScaleFactorMode);
        }

        on_currentTab_scaleFactorChanged(currentTab()->scaleFactor());
        on_currentTab_scaleModeChanged(currentTab()->scaleMode());
    }
}

void MainWindow::on_scaleFactor_returnPressed()
{
    currentTab()->setFocus();
}

void MainWindow::on_open_triggered()
{
    if(m_tabWidget->currentIndex() != -1)
    {
        const QString path = s_settings->mainWindow().openPath();
        const QString filePath = QFileDialog::getOpenFileName(this, tr("Open"), path, DocumentView::openFilter().join(";;"));

        if(!filePath.isEmpty())
        {
            open(filePath);
        }
    }
    else
    {
        on_openInNewTab_triggered();
    }
}

void MainWindow::on_openInNewTab_triggered()
{
    const QString path = s_settings->mainWindow().openPath();
    const QStringList filePaths = QFileDialog::getOpenFileNames(this, tr("Open in new tab"), path, DocumentView::openFilter().join(";;"));

    if(!filePaths.isEmpty())
    {
        disconnect(m_tabWidget, SIGNAL(currentChanged(int)), this, SLOT(on_tabWidget_currentChanged(int)));

        foreach(const QString& filePath, filePaths)
        {
            openInNewTab(filePath);
        }

        connect(m_tabWidget, SIGNAL(currentChanged(int)), this, SLOT(on_tabWidget_currentChanged(int)));

        on_tabWidget_currentChanged(m_tabWidget->currentIndex());
    }
}

void MainWindow::on_refresh_triggered()
{
    if(!currentTab()->refresh())
    {
        QMessageBox::warning(this, tr("Warning"), tr("Could not refresh '%1'.").arg(currentTab()->filePath()));
    }
}

void MainWindow::on_saveCopy_triggered()
{
    const QDir dir = QDir(s_settings->mainWindow().savePath());
    const QString fileName = QFileInfo(currentTab()->filePath()).fileName();
    const QString filePath = QFileDialog::getSaveFileName(this, tr("Save copy"), QFileInfo(dir, fileName).filePath(), currentTab()->saveFilter().join(";;"));

    if(!filePath.isEmpty())
    {
        if(currentTab()->save(filePath, false))
        {
            s_settings->mainWindow().setSavePath(QFileInfo(filePath).absolutePath());
        }
        else
        {
            QMessageBox::warning(this, tr("Warning"), tr("Could not save copy at '%1'.").arg(filePath));
        }
    }
}

void MainWindow::on_saveAs_triggered()
{
    const QString filePath = QFileDialog::getSaveFileName(this, tr("Save as"), currentTab()->filePath(), currentTab()->saveFilter().join(";;"));

    if(!filePath.isEmpty())
    {
        if(currentTab()->save(filePath, true))
        {
            open(filePath, currentTab()->currentPage());

            s_settings->mainWindow().setSavePath(QFileInfo(filePath).absolutePath());
        }
        else
        {
            QMessageBox::warning(this, tr("Warning"), tr("Could not save as '%1'.").arg(filePath));
        }
    }
}

void MainWindow::on_print_triggered()
{
    QPrinter* printer = PrintDialog::createPrinter();
    PrintDialog* printDialog = new PrintDialog(printer, this);

    printer->setDocName(QFileInfo(currentTab()->filePath()).completeBaseName());
    printer->setFullPage(true);

    printDialog->setMinMax(1, currentTab()->numberOfPages());
    printDialog->setOption(QPrintDialog::PrintToFile, false);

#if QT_VERSION >= QT_VERSION_CHECK(4,7,0)

    printDialog->setOption(QPrintDialog::PrintCurrentPage, true);

#endif // QT_VERSION

    if(printDialog->exec() == QDialog::Accepted)
    {

#if QT_VERSION >= QT_VERSION_CHECK(4,7,0)

        if(printDialog->printRange() == QPrintDialog::CurrentPage)
        {
            printer->setFromTo(currentTab()->currentPage(), currentTab()->currentPage());
        }

#endif // QT_VERSION

        if(!currentTab()->print(printer, printDialog->printOptions()))
        {
            QMessageBox::warning(this, tr("Warning"), tr("Could not print '%1'.").arg(currentTab()->filePath()));
        }
    }

    delete printDialog;
    delete printer;
}

void MainWindow::on_recentlyUsed_openTriggered(const QString& filePath)
{
    if(!jumpToPageOrOpenInNewTab(filePath, -1, true))
    {
        m_recentlyUsedMenu->removeOpenAction(filePath);
    }
}

void MainWindow::on_previousPage_triggered()
{
    currentTab()->previousPage();
}

void MainWindow::on_nextPage_triggered()
{
    currentTab()->nextPage();
}

void MainWindow::on_firstPage_triggered()
{
    currentTab()->firstPage();
}

void MainWindow::on_lastPage_triggered()
{
    currentTab()->lastPage();
}

void MainWindow::on_jumpToPage_triggered()
{
    bool ok = false;
    const int page = QInputDialog::getInt(this, tr("Jump to page"), tr("Page:"), currentTab()->currentPage(), 1, currentTab()->numberOfPages(), 1, &ok);

    if(ok)
    {
        currentTab()->jumpToPage(page);
    }
}

void MainWindow::on_jumpBackward_triggered()
{
    currentTab()->jumpBackward();
}

void MainWindow::on_jumpForward_triggered()
{
    currentTab()->jumpForward();
}

void MainWindow::on_search_triggered()
{
    m_searchDock->setVisible(true);

    m_searchLineEdit->selectAll();
    m_searchLineEdit->setFocus();
}

void MainWindow::on_findPrevious_triggered()
{
    if(!m_searchLineEdit->text().isEmpty())
    {
        currentTab()->findPrevious();
    }
}

void MainWindow::on_findNext_triggered()
{
    if(!m_searchLineEdit->text().isEmpty())
    {
        currentTab()->findNext();
    }
}

void MainWindow::on_cancelSearch_triggered()
{
    m_searchLineEdit->stopTimer();
    m_searchLineEdit->setProgress(0);

    m_searchDock->setVisible(false);

    for(int index = 0; index < m_tabWidget->count(); ++index)
    {
        tab(index)->cancelSearch();
    }
}

void MainWindow::on_copyToClipboardMode_triggered(bool checked)
{
    currentTab()->setRubberBandMode(checked ? CopyToClipboardMode : ModifiersMode);
}

void MainWindow::on_addAnnotationMode_triggered(bool checked)
{
    currentTab()->setRubberBandMode(checked ? AddAnnotationMode : ModifiersMode);
}

void MainWindow::on_settings_triggered()
{
    SettingsDialog* settingsDialog = new SettingsDialog(this);

    settingsDialog->resize(s_settings->mainWindow().settingsDialogSize(settingsDialog->sizeHint()));

    if(settingsDialog->exec() == QDialog::Accepted)
    {
        s_settings->sync();

        m_tabWidget->setTabPosition(static_cast< QTabWidget::TabPosition >(s_settings->mainWindow().tabPosition()));
        m_tabWidget->setTabBarPolicy(static_cast< TabWidget::TabBarPolicy >(s_settings->mainWindow().tabVisibility()));

        for(int index = 0; index < m_tabWidget->count(); ++index)
        {
            if(!tab(index)->refresh())
            {
                QMessageBox::warning(this, tr("Warning"), tr("Could not refresh '%1'.").arg(currentTab()->filePath()));
            }
        }
    }

    s_settings->mainWindow().setSettingsDialogSize(settingsDialog->size());

    delete settingsDialog;
}

void MainWindow::on_continuousMode_triggered(bool checked)
{
    currentTab()->setContinousMode(checked);
}

void MainWindow::on_twoPagesMode_triggered(bool checked)
{
    currentTab()->setLayoutMode(checked ? TwoPagesMode : SinglePageMode);
}

void MainWindow::on_twoPagesWithCoverPageMode_triggered(bool checked)
{
    currentTab()->setLayoutMode(checked ? TwoPagesWithCoverPageMode : SinglePageMode);
}

void MainWindow::on_multiplePagesMode_triggered(bool checked)
{
    currentTab()->setLayoutMode(checked ? MultiplePagesMode : SinglePageMode);
}

void MainWindow::on_zoomIn_triggered()
{
    currentTab()->zoomIn();
}

void MainWindow::on_zoomOut_triggered()
{
    currentTab()->zoomOut();
}

void MainWindow::on_originalSize_triggered()
{
    currentTab()->originalSize();
}

void MainWindow::on_fitToPageWidthMode_triggered(bool checked)
{
    currentTab()->setScaleMode(checked ? FitToPageWidthMode : ScaleFactorMode);
}

void MainWindow::on_fitToPageSizeMode_triggered(bool checked)
{
    currentTab()->setScaleMode(checked ? FitToPageSizeMode : ScaleFactorMode);
}

void MainWindow::on_rotateLeft_triggered()
{
    currentTab()->rotateLeft();
}

void MainWindow::on_rotateRight_triggered()
{
    currentTab()->rotateRight();
}

void MainWindow::on_invertColors_triggered(bool checked)
{
    currentTab()->setInvertColors(checked);
}

void MainWindow::on_fonts_triggered()
{
    QStandardItemModel* fontsModel = currentTab()->fontsModel();
    QDialog* dialog = new QDialog(this);

    QTableView* tableView = new QTableView(dialog);
    tableView->setModel(fontsModel);

    tableView->setAlternatingRowColors(true);
    tableView->setSortingEnabled(true);
    tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)

    tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tableView->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

#else

    tableView->horizontalHeader()->setResizeMode(QHeaderView::ResizeToContents);
    tableView->verticalHeader()->setResizeMode(QHeaderView::ResizeToContents);

#endif // QT_VERSION

    tableView->verticalHeader()->setVisible(false);

    QDialogButtonBox* dialogButtonBox = new QDialogButtonBox(QDialogButtonBox::Ok, Qt::Horizontal, dialog);
    connect(dialogButtonBox, SIGNAL(accepted()), dialog, SLOT(accept()));
    connect(dialogButtonBox, SIGNAL(rejected()), dialog, SLOT(reject()));

    dialog->setLayout(new QVBoxLayout(dialog));
    dialog->layout()->addWidget(tableView);
    dialog->layout()->addWidget(dialogButtonBox);

    dialog->resize(s_settings->mainWindow().fontsDialogSize(dialog->sizeHint()));
    dialog->exec();
    s_settings->mainWindow().setFontsDialogSize(dialog->size());

    delete dialog;
    delete fontsModel;
}

void MainWindow::on_fullscreen_triggered(bool checked)
{
    if(checked)
    {
        m_fullscreenAction->setData(saveGeometry());

        showFullScreen();
    }
    else
    {
        restoreGeometry(m_fullscreenAction->data().toByteArray());

        showNormal();

        restoreGeometry(m_fullscreenAction->data().toByteArray());
    }
}

void MainWindow::on_presentation_triggered()
{
    currentTab()->startPresentation();
}

void MainWindow::on_previousTab_triggered()
{
    if(m_tabWidget->currentIndex() > 0)
    {
        m_tabWidget->setCurrentIndex(m_tabWidget->currentIndex() - 1);
    }
    else
    {
        m_tabWidget->setCurrentIndex(m_tabWidget->count() - 1);
    }
}

void MainWindow::on_nextTab_triggered()
{
    if(m_tabWidget->currentIndex() < m_tabWidget->count() - 1)
    {
        m_tabWidget->setCurrentIndex(m_tabWidget->currentIndex() + 1);
    }
    else
    {
        m_tabWidget->setCurrentIndex(0);
    }
}

void MainWindow::on_closeTab_triggered()
{
    savePerFileSettings(currentTab());

    delete m_tabWidget->currentWidget();
}

void MainWindow::on_closeAllTabs_triggered()
{
    disconnect(m_tabWidget, SIGNAL(currentChanged(int)), this, SLOT(on_tabWidget_currentChanged(int)));

    while(m_tabWidget->count() > 0)
    {
        savePerFileSettings(tab(0));

        delete m_tabWidget->widget(0);
    }

    connect(m_tabWidget, SIGNAL(currentChanged(int)), this, SLOT(on_tabWidget_currentChanged(int)));

    on_tabWidget_currentChanged(-1);
}

void MainWindow::on_closeAllTabsButCurrentTab_triggered()
{
    DocumentView* newTab = currentTab();

    disconnect(m_tabWidget, SIGNAL(currentChanged(int)), this, SLOT(on_tabWidget_currentChanged(int)));

    m_tabWidget->removeTab(m_tabWidget->currentIndex());

    while(m_tabWidget->count() > 0)
    {
        savePerFileSettings(tab(0));

        delete m_tabWidget->widget(0);
    }

    connect(m_tabWidget, SIGNAL(currentChanged(int)), this, SLOT(on_tabWidget_currentChanged(int)));

    const QFileInfo fileInfo(newTab->filePath());

    const int index = m_tabWidget->addTab(newTab, fileInfo.completeBaseName());
    m_tabWidget->setTabToolTip(index, fileInfo.absoluteFilePath());
    m_tabWidget->setCurrentIndex(index);
}

void MainWindow::on_tabAction_triggered()
{
    for(int index = 0; index < m_tabWidget->count(); ++index)
    {
        if(sender()->parent() == m_tabWidget->widget(index))
        {
            m_tabWidget->setCurrentIndex(index);

            break;
        }
    }
}

void MainWindow::on_tabShortcut_activated()
{
    for(int index = 0; index < 9; ++index)
    {
        if(sender() == m_tabShortcuts[index])
        {
            m_tabWidget->setCurrentIndex(index);

            break;
        }
    }
}

void MainWindow::on_previousBookmark_triggered()
{
    const BookmarkMenu* bookmark = bookmarkForCurrentTab();

    if(bookmark != 0)
    {
        QList< int > pages = bookmark->pages();

        if(!pages.isEmpty())
        {
            qSort(pages);

            QList< int >::const_iterator lowerBound = --qLowerBound(pages, currentTab()->currentPage());

            if(lowerBound >= pages.constBegin())
            {
                currentTab()->jumpToPage(*lowerBound);
            }
            else
            {
                currentTab()->jumpToPage(pages.last());
            }
        }
    }
}

void MainWindow::on_nextBookmark_triggered()
{
    const BookmarkMenu* bookmark = bookmarkForCurrentTab();

    if(bookmark != 0)
    {
        QList< int > pages = bookmark->pages();

        if(!pages.isEmpty())
        {
            qSort(pages);

            QList< int >::const_iterator upperBound = qUpperBound(pages, currentTab()->currentPage());

            if(upperBound < pages.constEnd())
            {
                currentTab()->jumpToPage(*upperBound);
            }
            else
            {
                currentTab()->jumpToPage(pages.first());
            }
        }
    }
}

void MainWindow::on_addBookmark_triggered()
{
    BookmarkMenu* bookmark = bookmarkForCurrentTab();

    if(bookmark != 0)
    {
        bookmark->addJumpToPageAction(currentTab()->currentPage());
    }
    else
    {
        bookmark = new BookmarkMenu(currentTab()->filePath(), this);

        bookmark->addJumpToPageAction(currentTab()->currentPage());

        connect(bookmark, SIGNAL(openTriggered(QString)), SLOT(on_bookmark_openTriggered(QString)));
        connect(bookmark, SIGNAL(openInNewTabTriggered(QString)), SLOT(on_bookmark_openInNewTabTriggered(QString)));
        connect(bookmark, SIGNAL(jumpToPageTriggered(QString,int)), SLOT(on_bookmark_jumpToPageTriggered(QString,int)));

        m_bookmarksMenu->addMenu(bookmark);
    }
}

void MainWindow::on_removeBookmark_triggered()
{
    BookmarkMenu* bookmark = bookmarkForCurrentTab();

    if(bookmark != 0)
    {
        bookmark->removeJumpToPageAction(currentTab()->currentPage());
    }
}

void MainWindow::on_removeAllBookmarks_triggered()
{
    foreach(const QAction* action, m_bookmarksMenu->actions())
    {
        BookmarkMenu* bookmark = qobject_cast< BookmarkMenu* >(action->menu());

        if(bookmark != 0)
        {
            delete bookmark;
        }
    }
}

void MainWindow::on_bookmark_openTriggered(const QString& filePath)
{
    if(m_tabWidget->currentIndex() != -1)
    {
        open(filePath);
    }
    else
    {
        openInNewTab(filePath);
    }
}

void MainWindow::on_bookmark_openInNewTabTriggered(const QString& filePath)
{
    openInNewTab(filePath);
}

void MainWindow::on_bookmark_jumpToPageTriggered(const QString& filePath, int page)
{
    jumpToPageOrOpenInNewTab(filePath, page);
}

void MainWindow::on_contents_triggered()
{
    QDialog* dialog = new QDialog(this);

    QTextBrowser* textBrowser = new QTextBrowser(dialog);
    textBrowser->setSearchPaths(QStringList() << QDir(QApplication::applicationDirPath()).filePath("data") << DATA_INSTALL_PATH);
    textBrowser->setSource(QUrl("help.html"));

    QDialogButtonBox* dialogButtonBox = new QDialogButtonBox(QDialogButtonBox::Ok, Qt::Horizontal, dialog);
    connect(dialogButtonBox, SIGNAL(accepted()), dialog, SLOT(accept()));
    connect(dialogButtonBox, SIGNAL(rejected()), dialog, SLOT(reject()));

    dialog->setLayout(new QVBoxLayout(dialog));
    dialog->layout()->addWidget(textBrowser);
    dialog->layout()->addWidget(dialogButtonBox);

    dialog->resize(s_settings->mainWindow().contentsDialogSize(dialog->sizeHint()));
    dialog->exec();
    s_settings->mainWindow().setContentsDialogSize(dialog->size());

    delete dialog;
}

void MainWindow::on_about_triggered()
{
    QMessageBox::about(this, tr("About qpdfview"), (tr("<p><b>qpdfview %1</b></p><p>qpdfview is a tabbed document viewer using Qt.</p>"
                                                      "<p>This version includes:"
                                                      "<ul>")
#ifdef WITH_PDF
                                                      + tr("<li>PDF support using Poppler</li>")
#endif // WITH_PDF
#ifdef WITH_PS
                                                      + tr("<li>PS support using libspectre</li>")
#endif // WITH_PS
#ifdef WITH_DJVU
                                                      + tr("<li>DjVu support using DjVuLibre</li>")
#endif // WITH_DJVU
#ifdef WITH_CUPS
                                                      + tr("<li>Printing support using CUPS</li>")
#endif // WITH_CUPS
                                                      + tr("</ul>"
                                                      "<p>See <a href=\"https://launchpad.net/qpdfview\">launchpad.net/qpdfview</a> for more information.</p><p>&copy; 2012-2013 The qpdfview developers</p>")).arg(QApplication::applicationVersion()));
}

void MainWindow::on_searchInitiated(const QString& text, bool allTabs)
{
    if(!text.isEmpty())
    {
        if(allTabs)
        {
            for(int index = 0; index < m_tabWidget->count(); ++index)
            {
                tab(index)->startSearch(text, m_matchCaseCheckBox->isChecked());
            }
        }
        else
        {
            currentTab()->startSearch(text, m_matchCaseCheckBox->isChecked());
        }
    }
}

void MainWindow::on_highlightAll_clicked(bool checked)
{
    currentTab()->setHighlightAll(checked);
}

void MainWindow::on_model_reset()
{
    if(m_outlineView->header()->count() > 0)
    {
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)

        m_outlineView->header()->setSectionResizeMode(0, QHeaderView::Stretch);

#else

        m_outlineView->header()->setResizeMode(0, QHeaderView::Stretch);

#endif // QT_VERSION
    }

    if(m_outlineView->header()->count() > 1)
    {
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)

        m_outlineView->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

#else

        m_outlineView->header()->setResizeMode(1, QHeaderView::ResizeToContents);

#endif // QT_VERSION
    }

    m_outlineView->header()->setStretchLastSection(false);
    m_outlineView->header()->setVisible(false);

#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)

    m_propertiesView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_propertiesView->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

#else

    m_propertiesView->horizontalHeader()->setResizeMode(QHeaderView::Stretch);
    m_propertiesView->verticalHeader()->setResizeMode(QHeaderView::ResizeToContents);

#endif // QT_VERSION

    m_propertiesView->horizontalHeader()->setVisible(false);
    m_propertiesView->verticalHeader()->setVisible(false);
}

void MainWindow::on_outline_clicked(const QModelIndex& index)
{
    bool ok = false;
    const int page = m_outlineView->model()->data(index, Qt::UserRole + 1).toInt(&ok);
    const qreal left = m_outlineView->model()->data(index, Qt::UserRole + 2).toReal();
    const qreal top = m_outlineView->model()->data(index, Qt::UserRole + 3).toReal();

    if(ok)
    {
        currentTab()->jumpToPage(page, true, left, top);
    }
}

void MainWindow::on_thumbnails_dockLocationChanged(Qt::DockWidgetArea area)
{
    for(int index = 0; index < m_tabWidget->count(); ++index)
    {
        tab(index)->setThumbnailsOrientation(area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea ? Qt::Horizontal : Qt::Vertical);
    }
}

void MainWindow::on_thumbnails_verticalScrollBar_valueChanged(int value)
{
    Q_UNUSED(value);

    if(m_thumbnailsView->scene() != 0)
    {
        const QRectF visibleRect = m_thumbnailsView->mapToScene(m_thumbnailsView->viewport()->rect()).boundingRect();

        foreach(ThumbnailItem* page, currentTab()->thumbnailItems())
        {
            if(!page->boundingRect().translated(page->pos()).intersects(visibleRect))
            {
                page->cancelRender();
            }
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveTabs();
    saveBookmarks();

    for(int index = 0; index < m_tabWidget->count(); ++index)
    {
        savePerFileSettings(tab(index));
    }

    m_searchDock->setVisible(false);

    s_settings->mainWindow().setRecentlyUsed(s_settings->mainWindow().trackRecentlyUsed() ? m_recentlyUsedMenu->filePaths() : QStringList());

    s_settings->documentView().setMatchCase(m_matchCaseCheckBox->isChecked());

    s_settings->mainWindow().setGeometry(m_fullscreenAction->isChecked() ? m_fullscreenAction->data().toByteArray() : saveGeometry());
    s_settings->mainWindow().setState(saveState());

    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if(event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    if(event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();

        disconnect(m_tabWidget, SIGNAL(currentChanged(int)), this, SLOT(on_tabWidget_currentChanged(int)));

        foreach(const QUrl& url, event->mimeData()->urls())
        {
#if QT_VERSION >= QT_VERSION_CHECK(4,8,0)
            if(url.isLocalFile())
#else
            if(url.scheme() == "file")
#endif // QT_VERSION
            {
                openInNewTab(url.toLocalFile());
            }
        }

        connect(m_tabWidget, SIGNAL(currentChanged(int)), this, SLOT(on_tabWidget_currentChanged(int)));

        on_tabWidget_currentChanged(m_tabWidget->currentIndex());
    }
}

DocumentView* MainWindow::currentTab() const
{
    return qobject_cast< DocumentView* >(m_tabWidget->currentWidget());
}

DocumentView* MainWindow::tab(int index) const
{
    return qobject_cast< DocumentView* >(m_tabWidget->widget(index));
}

bool MainWindow::senderIsCurrentTab() const
{
     return sender() == m_tabWidget->currentWidget() || qobject_cast< DocumentView* >(sender()) == 0;
}

QString MainWindow::windowTitleSuffixForCurrentTab() const
{
    if(s_settings->mainWindow().currentPageInWindowTitle())
    {
        return QString(" (%1 / %2) - qpdfview").arg(currentTab()->currentPage()).arg(currentTab()->numberOfPages());
    }
    else
    {
        return QLatin1String(" - qpdfview");
    }
}

BookmarkMenu* MainWindow::bookmarkForCurrentTab() const
{
    foreach(QAction* action, m_bookmarksMenu->actions())
    {
        BookmarkMenu* bookmark = qobject_cast< BookmarkMenu* >(action->menu());

        if(bookmark != 0)
        {
            if(QFileInfo(bookmark->filePath()).absoluteFilePath() == QFileInfo(currentTab()->filePath()).absoluteFilePath())
            {
                return bookmark;
            }
        }
    }

    return 0;
}

void MainWindow::createWidgets()
{
    m_tabWidget = new TabWidget(this);

    m_tabWidget->setDocumentMode(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setElideMode(Qt::ElideRight);

    m_tabWidget->setTabPosition(static_cast< QTabWidget::TabPosition >(s_settings->mainWindow().tabPosition()));
    m_tabWidget->setTabBarPolicy(static_cast< TabWidget::TabBarPolicy >(s_settings->mainWindow().tabVisibility()));

    setCentralWidget(m_tabWidget);

    connect(m_tabWidget, SIGNAL(currentChanged(int)), SLOT(on_tabWidget_currentChanged(int)));
    connect(m_tabWidget, SIGNAL(tabCloseRequested(int)), SLOT(on_tabWidget_tabCloseRequested(int)));

    // current page

    m_currentPageSpinBox = new SpinBox(this);

    m_currentPageSpinBox->setAlignment(Qt::AlignCenter);
    m_currentPageSpinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_currentPageSpinBox->setKeyboardTracking(false);

    connect(m_currentPageSpinBox, SIGNAL(editingFinished()), SLOT(on_currentPage_editingFinished()));
    connect(m_currentPageSpinBox, SIGNAL(returnPressed()), SLOT(on_currentPage_returnPressed()));

    m_currentPageAction = new QWidgetAction(this);

    m_currentPageAction->setObjectName(QLatin1String("currentPage"));
    m_currentPageAction->setDefaultWidget(m_currentPageSpinBox);

    // scale factor

    m_scaleFactorComboBox = new ComboBox(this);

    m_scaleFactorComboBox->setEditable(true);
    m_scaleFactorComboBox->setInsertPolicy(QComboBox::NoInsert);

    m_scaleFactorComboBox->addItem(tr("Page width"));
    m_scaleFactorComboBox->addItem(tr("Page size"));
    m_scaleFactorComboBox->addItem("50 %", 0.5);
    m_scaleFactorComboBox->addItem("75 %", 0.75);
    m_scaleFactorComboBox->addItem("100 %", 1.0);
    m_scaleFactorComboBox->addItem("125 %", 1.25);
    m_scaleFactorComboBox->addItem("150 %", 1.5);
    m_scaleFactorComboBox->addItem("200 %", 2.0);
    m_scaleFactorComboBox->addItem("400 %", 4.0);

    connect(m_scaleFactorComboBox, SIGNAL(activated(int)), SLOT(on_scaleFactor_activated(int)));
    connect(m_scaleFactorComboBox->lineEdit(), SIGNAL(editingFinished()), SLOT(on_scaleFactor_editingFinished()));
    connect(m_scaleFactorComboBox->lineEdit(), SIGNAL(returnPressed()), SLOT(on_scaleFactor_returnPressed()));

    m_scaleFactorAction = new QWidgetAction(this);

    m_scaleFactorAction->setObjectName(QLatin1String("scaleFactor"));
    m_scaleFactorAction->setDefaultWidget(m_scaleFactorComboBox);

    // search

    m_searchLineEdit = new SearchLineEdit(this);
    m_matchCaseCheckBox = new QCheckBox(tr("Match &case"), this);
    m_highlightAllCheckBox = new QCheckBox(tr("Highlight &all"), this);

    connect(m_searchLineEdit, SIGNAL(searchInitiated(QString,bool)), SLOT(on_searchInitiated(QString,bool)));
    connect(m_highlightAllCheckBox, SIGNAL(clicked(bool)), SLOT(on_highlightAll_clicked(bool)));
}

QAction* MainWindow::createAction(const QString& text, const QString& objectName, const QIcon& icon, const QKeySequence& shortcut, const char* member, bool checkable)
{
    QAction* action = new QAction(text, this);

    action->setObjectName(objectName);
    action->setIcon(icon);
    action->setShortcut(shortcut);

    if(!objectName.isEmpty())
    {
        ShortcutHandler::instance()->registerAction(action);
    }

    if(checkable)
    {
        action->setCheckable(true);

        connect(action, SIGNAL(triggered(bool)), member);
    }
    else
    {
        action->setIconVisibleInMenu(true);

        connect(action, SIGNAL(triggered()), member);
    }

    return action;
}

QAction* MainWindow::createAction(const QString& text, const QString& objectName, const QString& iconName, const QKeySequence& shortcut, const char* member, bool checkable)
{
    return createAction(text, objectName, QIcon::fromTheme(iconName, QIcon(QLatin1String(":icons/") + iconName + QLatin1String(".svg"))), shortcut, member, checkable);
}

void MainWindow::createActions()
{
    // file

    m_openAction = createAction(tr("&Open..."), QLatin1String("open"), QLatin1String("document-open"), QKeySequence::Open, SLOT(on_open_triggered()));
    m_openInNewTabAction = createAction(tr("Open in new &tab..."), QLatin1String("openInNewTab"), QLatin1String("tab-new"), QKeySequence::AddTab, SLOT(on_openInNewTab_triggered()));
    m_refreshAction = createAction(tr("&Refresh"), QLatin1String("refresh"), QLatin1String("view-refresh"), QKeySequence::Refresh, SLOT(on_refresh_triggered()));
    m_saveCopyAction = createAction(tr("&Save copy..."), QLatin1String("saveCopy"), QLatin1String("document-save"), QKeySequence::Save, SLOT(on_saveCopy_triggered()));
    m_saveAsAction = createAction(tr("Save &as..."), QLatin1String("saveAs"), QLatin1String("document-save-as"), QKeySequence::SaveAs, SLOT(on_saveAs_triggered()));
    m_printAction = createAction(tr("&Print..."), QLatin1String("print"), QLatin1String("document-print"), QKeySequence::Print, SLOT(on_print_triggered()));
    m_exitAction = createAction(tr("E&xit"), QLatin1String("exit"), QIcon::fromTheme("application-exit"), QKeySequence::Quit, SLOT(close()));

    // edit

    m_previousPageAction = createAction(tr("&Previous page"), QLatin1String("previousPage"), QLatin1String("go-previous"), QKeySequence(Qt::Key_Backspace), SLOT(on_previousPage_triggered()));
    m_nextPageAction = createAction(tr("&Next page"), QLatin1String("nextPage"), QLatin1String("go-next"), QKeySequence(Qt::Key_Space), SLOT(on_nextPage_triggered()));
    m_firstPageAction = createAction(tr("&First page"), QLatin1String("firstPage"), QLatin1String("go-first"), QKeySequence(Qt::Key_Home), SLOT(on_firstPage_triggered()));
    m_lastPageAction = createAction(tr("&Last page"), QLatin1String("lastPage"), QLatin1String("go-last"), QKeySequence(Qt::Key_End), SLOT(on_lastPage_triggered()));

    m_jumpToPageAction = createAction(tr("&Jump to page..."), QLatin1String("jumpToPage"), QLatin1String("go-jump"), QKeySequence(Qt::CTRL + Qt::Key_J), SLOT(on_jumpToPage_triggered()));

    m_jumpBackwardAction = createAction(tr("Jump &backward"), QLatin1String("jumpBackward"), QLatin1String("media-seek-backward"), QKeySequence(Qt::CTRL + Qt::Key_Return), SLOT(on_jumpBackward_triggered()));
    m_jumpForwardAction = createAction(tr("Jump for&ward"), QLatin1String("jumpForward"), QLatin1String("media-seek-forward"), QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_Return), SLOT(on_jumpForward_triggered()));

    m_searchAction = createAction(tr("&Search..."), QLatin1String("search"), QLatin1String("edit-find"), QKeySequence::Find, SLOT(on_search_triggered()));
    m_findPreviousAction = createAction(tr("Find previous"), QLatin1String("findPrevious"), QLatin1String("go-up"), QKeySequence::FindPrevious, SLOT(on_findPrevious_triggered()));
    m_findNextAction = createAction(tr("Find next"), QLatin1String("findNext"), QLatin1String("go-down"), QKeySequence::FindNext, SLOT(on_findNext_triggered()));
    m_cancelSearchAction = createAction(tr("Cancel search"), QLatin1String("cancelSearch"), QLatin1String("process-stop"), QKeySequence(Qt::Key_Escape), SLOT(on_cancelSearch_triggered()));

    m_copyToClipboardModeAction = createAction(tr("&Copy to clipboard"), QLatin1String("copyToClipboardMode"), QLatin1String("edit-copy"), QKeySequence(Qt::CTRL + Qt::Key_C), SLOT(on_copyToClipboardMode_triggered(bool)), true);
    m_addAnnotationModeAction = createAction(tr("&Add annotation"), QLatin1String("addAnnotationMode"), QLatin1String("mail-attachment"), QKeySequence(Qt::CTRL + Qt::Key_A), SLOT(on_addAnnotationMode_triggered(bool)), true);

    m_settingsAction = createAction(tr("Settings..."), QString(), QIcon(), QKeySequence(), SLOT(on_settings_triggered()));

    // view

    m_continuousModeAction = createAction(tr("&Continuous"), QLatin1String("continuousMode"), QIcon(":icons/continuous.svg"), QKeySequence(Qt::CTRL + Qt::Key_7), SLOT(on_continuousMode_triggered(bool)), true);
    m_twoPagesModeAction = createAction(tr("&Two pages"), QLatin1String("twoPagesMode"), QIcon(":icons/two-pages.svg"), QKeySequence(Qt::CTRL + Qt::Key_6), SLOT(on_twoPagesMode_triggered(bool)), true);
    m_twoPagesWithCoverPageModeAction = createAction(tr("Two pages &with cover page"), QLatin1String("twoPagesWithCoverPageMode"), QIcon(":icons/two-pages-with-cover-page.svg"), QKeySequence(Qt::CTRL + Qt::Key_5), SLOT(on_twoPagesWithCoverPageMode_triggered(bool)), true);
    m_multiplePagesModeAction = createAction(tr("&Multiple pages"), QLatin1String("multiplePagesMode"), QIcon(":icons/multiple-pages.svg"), QKeySequence(Qt::CTRL + Qt::Key_4), SLOT(on_multiplePagesMode_triggered(bool)), true);

    m_zoomInAction = createAction(tr("Zoom &in"), QLatin1String("zoomIn"), QLatin1String("zoom-in"), QKeySequence(Qt::CTRL + Qt::Key_Up), SLOT(on_zoomIn_triggered()));
    m_zoomOutAction = createAction(tr("Zoom &out"), QLatin1String("zoomOut"), QLatin1String("zoom-out"), QKeySequence(Qt::CTRL + Qt::Key_Down), SLOT(on_zoomOut_triggered()));
    m_originalSizeAction = createAction(tr("Original &size"), QLatin1String("originalSize"), QLatin1String("zoom-original"), QKeySequence(Qt::CTRL + Qt::Key_0), SLOT(on_originalSize_triggered()));

    m_fitToPageWidthModeAction = createAction(tr("Fit to page width"), QLatin1String("fitToPageWidthMode"), QIcon(":icons/fit-to-page-width.svg"), QKeySequence(Qt::CTRL + Qt::Key_9), SLOT(on_fitToPageWidthMode_triggered(bool)), true);
    m_fitToPageSizeModeAction = createAction(tr("Fit to page size"), QLatin1String("fitToPageSizeMode"), QIcon(":icons/fit-to-page-size.svg"), QKeySequence(Qt::CTRL + Qt::Key_8), SLOT(on_fitToPageSizeMode_triggered(bool)), true);

    m_rotateLeftAction = createAction(tr("Rotate &left"), QLatin1String("rotateLeft"), QLatin1String("object-rotate-left"), QKeySequence(Qt::CTRL + Qt::Key_Left), SLOT(on_rotateLeft_triggered()));
    m_rotateRightAction = createAction(tr("Rotate &right"), QLatin1String("rotateRight"), QLatin1String("object-rotate-right"), QKeySequence(Qt::CTRL + Qt::Key_Right), SLOT(on_rotateRight_triggered()));

    m_invertColorsAction = createAction(tr("Invert colors"), QLatin1String("invertColors"), QIcon(), QKeySequence(Qt::CTRL + Qt::Key_I), SLOT(on_invertColors_triggered(bool)), true);

    m_fontsAction = createAction(tr("Fonts..."), QString(), QIcon(), QKeySequence(), SLOT(on_fonts_triggered()));

    m_fullscreenAction = createAction(tr("&Fullscreen"), QLatin1String("fullscreen"), QLatin1String("view-fullscreen"), QKeySequence(Qt::Key_F11), SLOT(on_fullscreen_triggered(bool)), true);
    m_presentationAction = createAction(tr("&Presentation..."), QLatin1String("presentation"), QLatin1String("x-office-presentation"), QKeySequence(Qt::Key_F12), SLOT(on_presentation_triggered()));

    // tabs

    m_previousTabAction = createAction(tr("&Previous tab"), QLatin1String("previousTab"), QIcon(), QKeySequence::PreviousChild, SLOT(on_previousTab_triggered()));
    m_nextTabAction = createAction(tr("&Next tab"), QLatin1String("nextTab"), QIcon(), QKeySequence::NextChild, SLOT(on_nextTab_triggered()));

    m_closeTabAction = createAction(tr("&Close tab"), QLatin1String("closeTab"), QIcon::fromTheme("window-close"), QKeySequence(Qt::CTRL + Qt::Key_W), SLOT(on_closeTab_triggered()));
    m_closeAllTabsAction = createAction(tr("Close &all tabs"), QLatin1String("closeAllTabs"), QIcon(), QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_W), SLOT(on_closeAllTabs_triggered()));
    m_closeAllTabsButCurrentTabAction = createAction(tr("Close all tabs &but current tab"), QLatin1String("closeAllTabsButCurrent"), QIcon(), QKeySequence(Qt::CTRL + Qt::ALT + Qt::Key_W), SLOT(on_closeAllTabsButCurrentTab_triggered()));

    // tab shortcuts

    for(int index = 0; index < 9; ++index)
    {
        m_tabShortcuts[index] = new QShortcut(QKeySequence(Qt::ALT + Qt::Key_1 + index), this, SLOT(on_tabShortcut_activated()));
    }

    // bookmarks

    m_previousBookmarkAction = createAction(tr("&Previous bookmark"), QLatin1String("previousBookmarkAction"), QIcon(), QKeySequence(Qt::CTRL + Qt::Key_PageUp), SLOT(on_previousBookmark_triggered()));
    m_nextBookmarkAction = createAction(tr("&Next bookmark"), QLatin1String("nextBookmarkAction"), QIcon(), QKeySequence(Qt::CTRL + Qt::Key_PageDown), SLOT(on_nextBookmark_triggered()));

    m_addBookmarkAction = createAction(tr("&Add bookmark"), QLatin1String("addBookmark"), QIcon(), QKeySequence(Qt::CTRL + Qt::Key_B), SLOT(on_addBookmark_triggered()));
    m_removeBookmarkAction = createAction(tr("&Remove bookmark"), QLatin1String("removeBookmark"), QIcon(), QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_B), SLOT(on_removeBookmark_triggered()));
    m_removeAllBookmarksAction = createAction(tr("Remove all bookmarks"), QLatin1String("removeAllBookmark"), QIcon(), QKeySequence(Qt::CTRL + Qt::ALT + Qt::Key_B), SLOT(on_removeAllBookmarks_triggered()));

    // help

    m_contentsAction = createAction(tr("&Contents"), QLatin1String("contents"), QIcon::fromTheme("help-contents"), QKeySequence::HelpContents, SLOT(on_contents_triggered()));
    m_aboutAction = createAction(tr("&About"), QString(), QIcon::fromTheme("help-about"), QKeySequence(), SLOT(on_about_triggered()));
}

QToolBar* MainWindow::createToolBar(const QString& text, const QString& objectName, const QStringList& actionNames, const QList< QAction* >& actions)
{
    QToolBar* toolBar = addToolBar(text);

    toolBar->setObjectName(objectName);

    foreach(const QString& actionName, actionNames)
    {
        if(actionName == QLatin1String("separator"))
        {
            toolBar->addSeparator();

            continue;
        }

        foreach(QAction* action, actions)
        {
            if(actionName == action->objectName())
            {
                toolBar->addAction(action);

                break;
            }
        }
    }

    return toolBar;
}

void MainWindow::createToolBars()
{
    m_fileToolBar = createToolBar(tr("&File"), QLatin1String("fileToolBar"), s_settings->mainWindow().fileToolBar(),
                                  QList< QAction* >() << m_openAction << m_openInNewTabAction << m_refreshAction << m_saveCopyAction << m_saveAsAction << m_printAction);

    m_editToolBar = createToolBar(tr("&Edit"), QLatin1String("editToolBar"), s_settings->mainWindow().editToolBar(),
                                  QList< QAction* >() << m_currentPageAction << m_previousPageAction << m_nextPageAction << m_firstPageAction << m_lastPageAction << m_jumpToPageAction << m_searchAction << m_jumpBackwardAction << m_jumpForwardAction << m_copyToClipboardModeAction << m_addAnnotationModeAction);

    m_viewToolBar = createToolBar(tr("&View"), QLatin1String("viewToolBar"), s_settings->mainWindow().viewToolBar(),
                                  QList< QAction* >() << m_scaleFactorAction << m_continuousModeAction << m_twoPagesModeAction << m_twoPagesWithCoverPageModeAction << m_multiplePagesModeAction << m_zoomInAction << m_zoomOutAction << m_originalSizeAction << m_fitToPageWidthModeAction << m_fitToPageSizeModeAction << m_rotateLeftAction << m_rotateRightAction << m_fullscreenAction << m_presentationAction);
}

QDockWidget* MainWindow::createDock(const QString& text, const QString& objectName, const QKeySequence& toggleViewShortcut)
{
    QDockWidget* dock = new QDockWidget(text, this);

    dock->setObjectName(objectName);

    addDockWidget(Qt::LeftDockWidgetArea, dock);

    dock->toggleViewAction()->setObjectName(objectName + QLatin1String("ToggleView"));
    dock->toggleViewAction()->setShortcut(toggleViewShortcut);

    ShortcutHandler::instance()->registerAction(dock->toggleViewAction());

    dock->hide();

    return dock;
}

void MainWindow::createDocks()
{
    // outline

    m_outlineDock = createDock(tr("&Outline"), QLatin1String("outlineDock"), QKeySequence(Qt::Key_F6));

    m_outlineView = new TreeView(this);
    m_outlineView->setAlternatingRowColors(true);
    m_outlineView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_outlineView->setSelectionBehavior(QAbstractItemView::SelectRows);

    connect(m_outlineView, SIGNAL(clicked(QModelIndex)), SLOT(on_outline_clicked(QModelIndex)));

    m_outlineDock->setWidget(m_outlineView);

    // properties

    m_propertiesDock = createDock(tr("&Properties"), QLatin1String("propertiesDock"), QKeySequence(Qt::Key_F7));

    m_propertiesView = new QTableView(this);
    m_propertiesView->setAlternatingRowColors(true);
    m_propertiesView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_propertiesDock->setWidget(m_propertiesView);

    // thumbnails

    m_thumbnailsDock = createDock(tr("&Thumbnails"), QLatin1String("thumbnailsDock"), QKeySequence(Qt::Key_F8));

    connect(m_thumbnailsDock, SIGNAL(dockLocationChanged(Qt::DockWidgetArea)), SLOT(on_thumbnails_dockLocationChanged(Qt::DockWidgetArea)));

    m_thumbnailsView = new QGraphicsView(this);

    connect(m_thumbnailsView->verticalScrollBar(), SIGNAL(valueChanged(int)), SLOT(on_thumbnails_verticalScrollBar_valueChanged(int)));

    m_thumbnailsDock->setWidget(m_thumbnailsView);

    // search

    m_searchDock = new QDockWidget(tr("&Search"), this);
    m_searchDock->setObjectName(QLatin1String("searchDock"));
    m_searchDock->setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);

    addDockWidget(Qt::BottomDockWidgetArea, m_searchDock);

    m_searchWidget = new QWidget(this);

    QToolButton* findPreviousButton = new QToolButton(m_searchWidget);
    findPreviousButton->setAutoRaise(true);
    findPreviousButton->setDefaultAction(m_findPreviousAction);

    QToolButton* findNextButton = new QToolButton(m_searchWidget);
    findNextButton->setAutoRaise(true);
    findNextButton->setDefaultAction(m_findNextAction);

    QToolButton* cancelSearchButton = new QToolButton(m_searchWidget);
    cancelSearchButton->setAutoRaise(true);
    cancelSearchButton->setDefaultAction(m_cancelSearchAction);

    QGridLayout* searchLayout = new QGridLayout(m_searchWidget);
    searchLayout->setRowStretch(2, 1);
    searchLayout->setColumnStretch(2, 1);
    searchLayout->addWidget(m_searchLineEdit, 0, 0, 1, 6);
    searchLayout->addWidget(m_matchCaseCheckBox, 1, 0);
    searchLayout->addWidget(m_highlightAllCheckBox, 1, 1);
    searchLayout->addWidget(findPreviousButton, 1, 3);
    searchLayout->addWidget(findNextButton, 1, 4);
    searchLayout->addWidget(cancelSearchButton, 1, 5);

    m_searchDock->setWidget(m_searchWidget);

    connect(m_searchDock, SIGNAL(visibilityChanged(bool)), m_findPreviousAction, SLOT(setEnabled(bool)));
    connect(m_searchDock, SIGNAL(visibilityChanged(bool)), m_findNextAction, SLOT(setEnabled(bool)));
    connect(m_searchDock, SIGNAL(visibilityChanged(bool)), m_cancelSearchAction, SLOT(setEnabled(bool)));

    m_searchDock->setVisible(false);

    m_findPreviousAction->setEnabled(false);
    m_findNextAction->setEnabled(false);
    m_cancelSearchAction->setEnabled(false);
}

void MainWindow::createMenus()
{
    // file

    m_fileMenu = menuBar()->addMenu(tr("&File"));
    m_fileMenu->addActions(QList< QAction* >() << m_openAction << m_openInNewTabAction);

    m_recentlyUsedMenu = new RecentlyUsedMenu(s_settings->mainWindow().recentlyUsedCount(), this);

    if(s_settings->mainWindow().trackRecentlyUsed())
    {
        foreach(const QString& filePath, s_settings->mainWindow().recentlyUsed())
        {
            m_recentlyUsedMenu->addOpenAction(filePath);
        }

        connect(m_recentlyUsedMenu, SIGNAL(openTriggered(QString)), SLOT(on_recentlyUsed_openTriggered(QString)));

        m_fileMenu->addMenu(m_recentlyUsedMenu);

        QToolButton* openToolButton = qobject_cast< QToolButton* >(m_fileToolBar->widgetForAction(m_openAction));
        if(openToolButton != 0)
        {
            openToolButton->setMenu(m_recentlyUsedMenu);
        }

        QToolButton* openInNewTabToolButton = qobject_cast< QToolButton* >(m_fileToolBar->widgetForAction(m_openInNewTabAction));
        if(openInNewTabToolButton != 0)
        {
            openInNewTabToolButton->setMenu(m_recentlyUsedMenu);
        }
    }

    m_fileMenu->addActions(QList< QAction* >() << m_refreshAction << m_saveCopyAction << m_saveAsAction << m_printAction);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_exitAction);

    // edit

    m_editMenu = menuBar()->addMenu(tr("&Edit"));
    m_editMenu->addActions(QList< QAction* >() << m_previousPageAction << m_nextPageAction << m_firstPageAction << m_lastPageAction << m_jumpToPageAction);
    m_editMenu->addSeparator();
    m_editMenu->addActions(QList< QAction* >() << m_jumpBackwardAction << m_jumpForwardAction);
    m_editMenu->addSeparator();
    m_editMenu->addActions(QList< QAction* >() << m_searchAction << m_findPreviousAction << m_findNextAction << m_cancelSearchAction);
    m_editMenu->addSeparator();
    m_editMenu->addActions(QList< QAction* >() << m_copyToClipboardModeAction << m_addAnnotationModeAction);
    m_editMenu->addSeparator();
    m_editMenu->addAction(m_settingsAction);

    // view

    m_viewMenu = menuBar()->addMenu(tr("&View"));
    m_viewMenu->addActions(QList< QAction* >() << m_continuousModeAction << m_twoPagesModeAction << m_twoPagesWithCoverPageModeAction << m_multiplePagesModeAction);
    m_viewMenu->addSeparator();
    m_viewMenu->addActions(QList< QAction* >() << m_zoomInAction << m_zoomOutAction << m_originalSizeAction << m_fitToPageWidthModeAction << m_fitToPageSizeModeAction);
    m_viewMenu->addSeparator();
    m_viewMenu->addActions(QList< QAction* >() << m_rotateLeftAction << m_rotateRightAction);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_invertColorsAction);
    m_viewMenu->addSeparator();

    QMenu* toolBarsMenu = m_viewMenu->addMenu(tr("&Tool bars"));
    toolBarsMenu->addActions(QList< QAction* >() << m_fileToolBar->toggleViewAction() << m_editToolBar->toggleViewAction() << m_viewToolBar->toggleViewAction());

    QMenu* docksMenu = m_viewMenu->addMenu(tr("&Docks"));
    docksMenu->addActions(QList< QAction* >() << m_outlineDock->toggleViewAction() << m_propertiesDock->toggleViewAction() << m_thumbnailsDock->toggleViewAction());

    m_viewMenu->addAction(m_fontsAction);
    m_viewMenu->addSeparator();
    m_viewMenu->addActions(QList< QAction* >() << m_fullscreenAction << m_presentationAction);

    // tabs

    m_tabsMenu = menuBar()->addMenu(tr("&Tabs"));
    m_tabsMenu->addActions(QList< QAction* >() << m_previousTabAction << m_nextTabAction);
    m_tabsMenu->addSeparator();
    m_tabsMenu->addActions(QList< QAction* >() << m_closeTabAction << m_closeAllTabsAction << m_closeAllTabsButCurrentTabAction);
    m_tabsMenu->addSeparator();

    // bookmarks

    m_bookmarksMenu = menuBar()->addMenu(tr("&Bookmarks"));
    m_bookmarksMenu->addActions(QList< QAction* >() << m_previousBookmarkAction << m_nextBookmarkAction);
    m_bookmarksMenu->addSeparator();
    m_bookmarksMenu->addActions(QList< QAction* >() << m_addBookmarkAction << m_removeBookmarkAction << m_removeAllBookmarksAction);
    m_bookmarksMenu->addSeparator();

    // help

    m_helpMenu = menuBar()->addMenu(tr("&Help"));
    m_helpMenu->addActions(QList< QAction* >() << m_contentsAction << m_aboutAction);
}

void MainWindow::createDatabase()
{
#ifdef WITH_SQL

#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)

    const QString path = QStandardPaths::writableLocation(QStandardPaths::DataLocation);

#else

    const QString path = QDesktopServices::storageLocation(QDesktopServices::DataLocation);

#endif // QT_VERSION

    QDir().mkpath(path);

    m_database = QSqlDatabase::addDatabase("QSQLITE");
    m_database.setDatabaseName(QDir(path).filePath("database"));
    m_database.open();

    if(m_database.isOpen())
    {
        m_database.transaction();

        const QStringList tables = m_database.tables();
        QSqlQuery query(m_database);

        // tabs

        if(!tables.contains("tabs_v2"))
        {
            query.exec("CREATE TABLE tabs_v2 "
                       "(filePath TEXT"
                       ",instanceName TEXT"
                       ",currentPage INTEGER"
                       ",continuousMode INTEGER"
                       ",layoutMode INTEGER"
                       ",scaleMode INTEGER"
                       ",scaleFactor REAL"
                       ",rotation INTEGER)");

            if(!query.isActive())
            {
                qDebug() << query.lastError();
            }
        }

        // bookmarks

        if(!tables.contains("bookmarks_v1"))
        {
            query.exec("CREATE TABLE bookmarks_v1 "
                       "(filePath TEXT"
                       ",pages TEXT)");

            if(!query.isActive())
            {
                qDebug() << query.lastError();
            }
        }

        // per-file settings

        if(!tables.contains("perfilesettings_v1"))
        {
            query.exec("CREATE TABLE perfilesettings_v1 "
                       "(lastUsed INTEGER"
                       ",filePath TEXT PRIMARY KEY"
                       ",currentPage INTEGER"
                       ",continuousMode INTEGER"
                       ",layoutMode INTEGER"
                       ",scaleMode INTEGER"
                       ",scaleFactor REAL"
                       ",rotation INTEGER)");

            if(!query.isActive())
            {
                qDebug() << query.lastError();
            }
        }

        if(s_settings->mainWindow().restorePerFileSettings())
        {
            query.exec("DELETE FROM perfilesettings_v1 WHERE filePath IN (SELECT filePath FROM perfilesettings_v1 ORDER BY lastUsed DESC LIMIT -1 OFFSET 1000)");
        }
        else
        {
            query.exec("DELETE FROM perfilesettings_v1");
        }

        if(!query.isActive())
        {
            qDebug() << query.lastError();
        }

        m_database.commit();
    }
    else
    {
        qDebug() << m_database.lastError();
    }

#endif // WITH_SQL
}

void MainWindow::restoreTabs()
{
#ifdef WITH_SQL

    if(m_database.isOpen())
    {
        m_database.transaction();

        QSqlQuery query(m_database);
        query.prepare("SELECT filePath,currentPage,continuousMode,layoutMode,scaleMode,scaleFactor,rotation FROM tabs_v2 WHERE instanceName==?");

        query.bindValue(0, objectName());

        query.exec();

        while(query.next())
        {
            if(!query.isActive())
            {
                qDebug() << query.lastError();
                break;
            }

            if(openInNewTab(query.value(0).toString()))
            {
                currentTab()->setContinousMode(static_cast< bool >(query.value(2).toUInt()));
                currentTab()->setLayoutMode(static_cast< LayoutMode >(query.value(3).toUInt()));

                currentTab()->setScaleMode(static_cast< ScaleMode >(query.value(4).toUInt()));
                currentTab()->setScaleFactor(query.value(5).toReal());

                currentTab()->setRotation(static_cast< Rotation >(query.value(6).toUInt()));

                currentTab()->jumpToPage(query.value(1).toInt());
            }
        }

        m_database.commit();
    }

#endif // WITH_SQL
}

void MainWindow::saveTabs()
{
#ifdef WITH_SQL

    if(m_database.isOpen())
    {
        m_database.transaction();

        QSqlQuery query(m_database);

        if(s_settings->mainWindow().restoreTabs())
        {
            query.prepare("DELETE FROM tabs_v2 WHERE instanceName==?");

            query.bindValue(0, objectName());

            query.exec();

            if(!query.isActive())
            {
                qDebug() << query.lastError();
            }

            query.prepare("INSERT INTO tabs_v2 "
                          "(filePath,instanceName,currentPage,continuousMode,layoutMode,scaleMode,scaleFactor,rotation)"
                          " VALUES (?,?,?,?,?,?,?,?)");

            for(int index = 0; index < m_tabWidget->count(); ++index)
            {
                query.bindValue(0, QFileInfo(tab(index)->filePath()).absoluteFilePath());
                query.bindValue(1, objectName());
                query.bindValue(2, tab(index)->currentPage());

                query.bindValue(3, static_cast< uint >(tab(index)->continousMode()));
                query.bindValue(4, static_cast< uint >(tab(index)->layoutMode()));

                query.bindValue(5, static_cast< uint >(tab(index)->scaleMode()));
                query.bindValue(6, tab(index)->scaleFactor());

                query.bindValue(7, static_cast< uint >(tab(index)->rotation()));

                query.exec();

                if(!query.isActive())
                {
                    qDebug() << query.lastError();
                    break;
                }
            }
        }
        else
        {
            query.exec("DELETE FROM tabs_v2");

            if(!query.isActive())
            {
                qDebug() << query.lastError();
            }
        }

        m_database.commit();
    }

#endif // WITH_SQL
}

void MainWindow::restoreBookmarks()
{
#ifdef WITH_SQL

    if(m_database.isOpen())
    {
        m_database.transaction();

        QSqlQuery query(m_database);
        query.exec("SELECT filePath,pages FROM bookmarks_v1");

        while(query.next())
        {
            if(!query.isActive())
            {
                qDebug() << query.lastError();
                break;
            }

            BookmarkMenu* bookmark = new BookmarkMenu(query.value(0).toString(), this);

            QStringList pages = query.value(1).toString().split(",", QString::SkipEmptyParts);

            foreach(const QString& page, pages)
            {
                bookmark->addJumpToPageAction(page.toInt());
            }

            connect(bookmark, SIGNAL(openTriggered(QString)), SLOT(on_bookmark_openTriggered(QString)));
            connect(bookmark, SIGNAL(openInNewTabTriggered(QString)), SLOT(on_bookmark_openInNewTabTriggered(QString)));
            connect(bookmark, SIGNAL(jumpToPageTriggered(QString,int)), SLOT(on_bookmark_jumpToPageTriggered(QString,int)));

            m_bookmarksMenu->addMenu(bookmark);
        }

        m_database.commit();
    }

#endif // WITH_SQL
}

void MainWindow::saveBookmarks()
{
#ifdef WITH_SQL

    if(m_database.isOpen())
    {
        m_database.transaction();

        QSqlQuery query(m_database);
        query.exec("DELETE FROM bookmarks_v1");

        if(!query.isActive())
        {
            qDebug() << query.lastError();
        }

        if(s_settings->mainWindow().restoreBookmarks())
        {
            query.prepare("INSERT INTO bookmarks_v1 "
                          "(filePath,pages)"
                          " VALUES (?,?)");

            foreach(const QAction* action, m_bookmarksMenu->actions())
            {
                const BookmarkMenu* bookmark = qobject_cast< BookmarkMenu* >(action->menu());

                if(bookmark != 0)
                {
                    QStringList pages;

                    foreach(const int page, bookmark->pages())
                    {
                        pages.append(QString::number(page));
                    }

                    query.bindValue(0, QFileInfo(bookmark->filePath()).absoluteFilePath());
                    query.bindValue(1, pages.join(","));

                    query.exec();

                    if(!query.isActive())
                    {
                        qDebug() << query.lastError();
                        break;
                    }
                }
            }
        }

        m_database.commit();
    }

#endif // WITH_SQL
}

void MainWindow::restorePerFileSettings(DocumentView* tab)
{
#ifdef WITH_SQL

    if(s_settings->mainWindow().restorePerFileSettings() && m_database.isOpen() && tab != 0)
    {
        m_database.transaction();

        QSqlQuery query(m_database);
        query.prepare("SELECT currentPage,continuousMode,layoutMode,scaleMode,scaleFactor,rotation FROM perfilesettings_v1 WHERE filePath==?");

        query.bindValue(0, QCryptographicHash::hash(QFileInfo(tab->filePath()).absoluteFilePath().toUtf8(), QCryptographicHash::Sha1).toBase64());

        query.exec();

        if(query.next())
        {
            tab->setContinousMode(query.value(1).toBool());
            tab->setLayoutMode(static_cast< LayoutMode >(query.value(2).toUInt()));

            tab->setScaleMode(static_cast< ScaleMode >(query.value(3).toUInt()));
            tab->setScaleFactor(query.value(4).toReal());

            tab->setRotation(static_cast< Rotation >(query.value(5).toUInt()));

            tab->jumpToPage(query.value(0).toInt(), false);
        }

        if(!query.isActive())
        {
            qDebug() << query.lastError();
        }

        m_database.commit();
    }

#else

    Q_UNUSED(tab);

#endif // WITH_SQL
}

void MainWindow::savePerFileSettings(const DocumentView* tab)
{
#ifdef WITH_SQL

    if(s_settings->mainWindow().restorePerFileSettings() && m_database.isOpen() && tab != 0)
    {
        m_database.transaction();

        QSqlQuery query(m_database);
        query.prepare("INSERT OR REPLACE INTO perfilesettings_v1 "
                      "(lastUsed,filePath,currentPage,continuousMode,layoutMode,scaleMode,scaleFactor,rotation)"
                      " VALUES (?,?,?,?,?,?,?,?)");

        query.bindValue(0, QDateTime::currentDateTime().toTime_t());

        query.bindValue(1, QCryptographicHash::hash(QFileInfo(tab->filePath()).absoluteFilePath().toUtf8(), QCryptographicHash::Sha1).toBase64());
        query.bindValue(2, tab->currentPage());

        query.bindValue(3, static_cast< uint >(tab->continousMode()));
        query.bindValue(4, static_cast< uint >(tab->layoutMode()));

        query.bindValue(5, static_cast< uint >(tab->scaleMode()));
        query.bindValue(6, tab->scaleFactor());

        query.bindValue(7, static_cast< uint >(tab->rotation()));

        query.exec();

        if(!query.isActive())
        {
            qDebug() << query.lastError();
        }

        m_database.commit();
    }

#else

    Q_UNUSED(tab);

#endif // WITH_SQL
}

#ifdef WITH_DBUS

MainWindowAdaptor::MainWindowAdaptor(MainWindow* mainWindow) : QDBusAbstractAdaptor(mainWindow)
{
}

bool MainWindowAdaptor::open(const QString& filePath, int page, const QRectF& highlight, bool quiet)
{
    return mainWindow()->open(filePath, page, highlight, quiet);
}

bool MainWindowAdaptor::openInNewTab(const QString& filePath, int page, const QRectF& highlight, bool quiet)
{
    return mainWindow()->openInNewTab(filePath, page, highlight, quiet);
}

bool MainWindowAdaptor::jumpToPageOrOpenInNewTab(const QString& filePath, int page, bool refreshBeforeJump, const QRectF& highlight, bool quiet)
{
    return mainWindow()->jumpToPageOrOpenInNewTab(filePath, page, refreshBeforeJump, highlight, quiet);
}

void MainWindowAdaptor::startSearch(const QString& text)
{
    mainWindow()->startSearch(text);
}

void MainWindowAdaptor::raiseAndActivate()
{
    mainWindow()->raise();
    mainWindow()->activateWindow();
}

MainWindow* MainWindowAdaptor::mainWindow() const
{
    return qobject_cast< MainWindow* >(parent());
}

# endif // WITH_DBUS
