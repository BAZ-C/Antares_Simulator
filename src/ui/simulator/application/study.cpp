/*
** Copyright 2007-2024, RTE (https://www.rte-france.com)
** See AUTHORS.txt
** SPDX-License-Identifier: MPL-2.0
** This file is part of Antares-Simulator,
** Adequacy and Performance assessment for interconnected energy networks.
**
** Antares_Simulator is free software: you can redistribute it and/or modify
** it under the terms of the Mozilla Public Licence 2.0 as published by
** the Mozilla Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** Antares_Simulator is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** Mozilla Public Licence 2.0 for more details.
**
** You should have received a copy of the Mozilla Public Licence 2.0
** along with Antares_Simulator. If not, see <https://opensource.org/license/mpl-2-0/>.
*/
#include "antares/study/study.h"
#include <yuni/core/system/environment.h>
#include <antares/study/area/area.h>
#include <antares/inifile/inifile.h>
#include <antares/io/statistics.h>

#include <ui/common/component/spotlight/spotlight.h>

#include "../toolbox/components/map/component.h"
#include "../toolbox/components/mainpanel.h"
#include "../toolbox/jobs.h"

#include "../toolbox/execute/execute.h"
#include "../windows/message.h"
#include <antares/date/date.h>
#include "../windows/saveas.h"
#include "main.h"
#include "menus.h"
#include "../windows/inspector/inspector.h"
#include <ui/common/lock.h>

#include <wx/wupdlock.h>
#include "wait.h"
#include <wx/busyinfo.h>
#include <ui/common/wx-wrapper.h>

#include "../windows/startupwizard.h"
#include <yuni/core/system/cpu.h>

#include "main/internal-data.h"
#include "antares/study/ui-runtimeinfos.h"
#include "antares/utils/utils.h"

#include <atomic>

using namespace Yuni;

#define SEP IO::Separator

namespace Antares
{
String LastPathForOpeningAFile;

wxString gLastOpenedStudyFolder;

//! Ref counter to allow memory flush
std::atomic<int> gMemoryFlushRefCount = 0;

Event<void()> OnStudyClosed;
Event<void()> OnStudyLoaded;
Event<void()> OnStudySaved;
Event<void()> OnStudySavedAs;
Event<void(Data::Study&)> OnStudyChanged;
Event<void()> OnStudyAreasChanged;
Event<void()> OnStudySettingsChanged;
Event<void(Data::Area*)> OnStudyAreaRename;
Event<void(Data::Area*)> OnStudyAreaAdded;
Event<void(Data::Area*)> OnStudyAreaDelete;
Event<void(Data::Area*)> OnStudyAreaColorChanged;
Event<void(Data::AreaLink*)> OnStudyLinkAdded;
Event<void(Data::AreaLink*)> OnStudyLinkChanged;
Event<void(Data::AreaLink*)> OnStudyLinkDelete;

Event<void()> OnStudySimulationSettingsChanged;
Event<void()> OnStudyNodalOptimizationChanged;

Event<void()> OnStudyThermalClusterCommonSettingsChanged;
Event<void(Data::ThermalCluster*)> OnStudyThermalClusterRenamed;
Event<void(Data::Area*)> OnStudyThermalClusterGroupChanged;

Event<void()> OnStudyRenewableClusterCommonSettingsChanged;
Event<void(Data::RenewableCluster*)> OnStudyRenewableClusterRenamed;
Event<void(Data::Area*)> OnStudyRenewableClusterGroupChanged;

Event<void()> OnStudyUpdatePlaylist;
Event<void(const void*)> OnInspectorRefresh;
Event<void()> OnStudyScenarioBuilderDataAreLoaded;
Event<void(const Data::Output::List&, const Data::Output::Ptr)> OnStudyUpdateOutputInfo;

Event<void(Data::BindingConstraint*)> OnStudyConstraintAdded;
Event<void(Data::BindingConstraint*)> OnStudyConstraintDelete;
Event<void(Data::BindingConstraint*)> OnStudyConstraintModified;

Event<void()> OnStudyBeginUpdate;
Event<void()> OnStudyEndUpdate;

Event<void()> OnMainNotebookChanged;

Event<void()> OnLayerNodeUIChanged;

Event<void(const wxString*)> OnMapLayerChanged;
Event<void(const wxString*)> OnMapLayerAdded;
Event<void(const wxString*)> OnMapLayerRemoved;
Event<void(const wxString*)> OnMapLayerRenamed;

bool SystemParameterHaveChanged = false;

bool GUIFlagInvalidateAreas = true;

Data::Output::List ListOfOutputsForTheCurrentStudy;

bool gStudyHasBeenModified = true;

volatile uint64_t gInMemoryRevisionIncrement = 0;

inline static void ResetLastOpenedFilepath()
{
#ifdef YUNI_OS_WINDOWS
    LastPathForOpeningAFile = "C:\\";
#else
    System::Environment::Read("HOME", LastPathForOpeningAFile);
#endif
}

static void TheSimulationIsComplete(const wxString& duration)
{
    if (IsGUIAboutToQuit() || !CurrentStudyIsValid())
        return;
    auto* mainFrm = Forms::ApplWnd::Instance();
    if (mainFrm)
    {
        Window::Message message(mainFrm,
                                wxT("Simulation"),
                                wxT("The simulation is finished"),
                                wxString()
                                  << wxT("Time to complete the simulation : ") << duration);
        message.add(Window::Message::btnContinue);
        message.add(Window::Message::btnViewResults);
        const uint userChoice = message.showModal();
        if (userChoice == Window::Message::btnViewResults)
            mainFrm->viewLatestOutput();
    }
}

static void finalizeSaveExport(Data::Study::Ptr study, Forms::ApplWnd& frame)
{
    Menu::AddRecentFile(frame.menuRecentFiles(),
                        wxStringFromUTF8(study->header.caption),
                        wxStringFromUTF8(study->folder.string()));

    // Rebuild the menu
    Menu::RebuildRecentFiles(frame.menuRecentFiles());

    gLastOpenedStudyFolder = wxStringFromUTF8(study->folder.string());

    RefreshListOfOutputsForTheCurrentStudy();

    frame.refreshMenuInput();
    frame.refreshMenuOptions(study);
    frame.refreshStudyLogs();
}

namespace // anonymous
{
class JobOpenStudy final : public Toolbox::Jobs::Job
{
public:
    JobOpenStudy(const wxString& folder) :
     Toolbox::Jobs::Job(wxT("Opening a study"),
                        wxT("Gathering informations about the study"),
                        "images/32x32/open.png"),
     pFolder(folder)
    {
        // reset IO statistics
        Statistics::Reset();
    }

    //! Destructor
    virtual ~JobOpenStudy()
    {
    }

    void folder(const wxString& f)
    {
        pFolder = f;
    }

protected:
    /*!
     * \brief Load a study from a folder
     */
    virtual bool executeTask()
    {
        logs.info();
        logs.checkpoint() << "Opening study...";

        // The folder
        String sFl;
        wxStringToString(pFolder, sFl);

        auto study = std::make_shared<Data::Study>(); // new study

        // Load all data
        Data::StudyLoadOptions options;
        options.loadOnlyNeeded = false;
        study->loadFromFolder(sFl, options);

        // Postflight
        logs.info();
        study->areas.ensureDataIsInitialized(study->parameters, options.loadOnlyNeeded);
        logs.info() << "The study is loaded.";

        gLastOpenedStudyFolder = pFolder;
        LastPathForOpeningAFile.clear() << sFl << SEP << "user";
        if (!IO::Directory::Exists(LastPathForOpeningAFile))
            LastPathForOpeningAFile = sFl;

        // We have a valid study. Go ahead
        // Keeping the study somewhere
        SetCurrentStudy(study);

        // The loading of the study may disable the Just-In-Time mecanism
        // (to ensure compatibility with old studies)
        // So we have to re-enable it.
        JIT::enabled = true;

        // The task is complete
        return true;
    }

private:
    //! The folder where the study is located
    wxString pFolder;

}; // class JobOpenStudy

class JobSaveStudy final : public Toolbox::Jobs::Job
{
public:
    JobSaveStudy(Data::Study::Ptr study,
                 const String& folder,
                 bool copyOutput = false,
                 bool copyUserData = false,
                 bool copyLogs = false) :
     Toolbox::Jobs::Job(wxT("Saving the study"), wxT("Saving the study"), "images/32x32/open.png"),
     study(study),
     pSaveAs(false),
     pShouldInvalidateStudy(false),
     pCopyOutput(copyOutput),
     pCopyUserData(copyUserData),
     pCopyLogs(copyLogs),
     pCount(128)
    {
        // reset IO statistics
        Statistics::Reset();

        // The main form
        Forms::ApplWnd& mainFrm = *Forms::ApplWnd::Instance();

        // Normalize the folder
        IO::Normalize(pFolder, folder);
        // Check if the scenario builder data can be destroyed
        pCanReleaseScenarioBuilder = !mainFrm.isScenarioBuilderOpened();
    }

    //! Destructor
    virtual ~JobSaveStudy()
    {
    }

    template<class StringT>
    void folder(const StringT& f)
    {
        pFolder = f;
    }

    void saveAs(bool b)
    {
        pSaveAs = b;
    }

    void shouldInvalidateStudy()
    {
        pShouldInvalidateStudy = true;
    }

public:
    //! Reference to the study to save
    Data::Study::Ptr study;

protected:
    /*!
     * \brief Load a study from a folder
     */
    virtual bool executeTask()
    {
        // Logs
        logs.notice() << "Saving the study...";
        logs.info() << "  Destination: " << pFolder;

        // making sure that all internal data are allocated
        study->areas.ensureDataIsInitialized(study->parameters, false);

        // Updating the number of logical cores to use when saving the study
        // so that the run window is up to date.
        study->getNumberOfCores(false, 0);

        if (pSaveAs || pShouldInvalidateStudy)
        {
            logs.info() << "Preparing study";
            // If the user save the study as something, we have to invalidate
            // all data (and load all missing files)
            study->areas.each([&](Data::Area& area) {
                logs.info() << "Preparing the area " << area.name;
                area.forceReload(true);
            });
            study->forceReload(true);
            // We have to mark the whole study as modified
            study->markAsModified();

            // The Scenario Builder Data must be available to perform a full save
            if (!study->scenarioRules)
                study->scenarioRulesCreate();
        }

        if (pSaveAs)
        {
            String targetOutput;
            String targetUser;
            String targetLogs;
            targetOutput << pFolder << SEP << "output";
            targetUser << pFolder << SEP << "user";
            targetLogs << pFolder << SEP << "logs";

            // cleaning
            IO::Directory::Remove(targetOutput);
            IO::Directory::Remove(targetUser);
            IO::Directory::Remove(targetLogs);

            if (!study->folder.empty())
            {
                // Warning : The target folder must not be cleaned up before
                // Copy the original folder
                String src;

                IO::Directory::CopyOnUpdateBind e;
                e.bind(this, &JobSaveStudy::onCopyFile);
                if (pCopyOutput)
                {
                    pTitle = "Copying files from the output folder";
                    src << study->folder << SEP << "output";
                    logs.info() << "   from " << src;
                    logs.info() << "   to   " << targetOutput;
                    if (targetOutput != src)
                        IO::Directory::Copy(src, targetOutput, true, true, e);
                }
                // The folder `user`
                if (pCopyUserData)
                {
                    pTitle = "Copying files from the user folder";
                    logs.info() << "Copying files from the user folder";
                    src.clear() << study->folder << SEP << "user";
                    logs.info() << "   from " << src;
                    logs.info() << "   to   " << targetUser;
                    if (targetUser != src)
                        IO::Directory::Copy(src, targetUser, true, true, e);
                }
                // The folder `logs`
                if (pCopyLogs)
                {
                    pTitle = "Copying log files";
                    logs.info() << "Copying log files";
                    src.clear() << study->folder << SEP << "logs";
                    logs.info() << "   from " << src;
                    logs.info() << "   to   " << targetLogs;
                    if (targetLogs != src)
                        IO::Directory::Copy(src, targetLogs, true, true, e);
                }
            } // not empty - copy files folders
        }     // save as

        // Save the study (only changes in the most cases)
        study->saveToFolder(pFolder);

        // Scenario Builder
        if (pCanReleaseScenarioBuilder && study->scenarioRules)
        {
            logs.debug()
              << "[ui] releasing the scenario builder data, since the page is not opened";
            study->scenarioRulesDestroy();
        }

        if (pSaveAs || LastPathForOpeningAFile.empty())
            LastPathForOpeningAFile.clear() << pFolder << SEP << "user";

        SystemParameterHaveChanged = true;

        // The task is complete
        return true;
    }

    bool onCopyFile(IO::Directory::CopyState state,
                    const String&,
                    const String&,
                    uint64_t count,
                    uint64_t total)
    {
        if (0 == (--pCount))
        {
            pCount = 128;
            switch (state)
            {
            case IO::Directory::cpsCopying:
            {
                if (!total) // prevent division by 0
                    total = 1;
                logs.info() << pTitle << " : " << ((100 * count) / total) << "%   ("
                            << (count / 1024 / 1024) << "Mo / " << (1 + (total / 1024 / 1024))
                            << "Mo)";
                return true;
            }
            case IO::Directory::cpsGatheringInformation:
            {
                logs.info() << pTitle << " : " << total << " items to consider...";
                return true;
            }
            }
        }
        return true;
    }

private:
    //! The folder where the study is located
    String pFolder;
    //! Our study
    bool pSaveAs;
    bool pShouldInvalidateStudy;
    const bool pCopyOutput;
    const bool pCopyUserData;
    const bool pCopyLogs;
    String pTitle;
    uint pCount;
    bool pCanReleaseScenarioBuilder;

}; // class JobSaveStudy

class JobExportMap final : public Toolbox::Jobs::Job
{
public:
    explicit JobExportMap(const String& path,
                          bool transparentBackground,
                          const wxColour& backgroundColor,
                          const std::list<uint16_t>& layers,
                          int nbSplitParts,
                          Antares::Map::mapImageFormat format) :
     Toolbox::Jobs::Job(wxT("Exporting the map"),
                        wxT("Exporting the map"),
                        "images/32x32/exportmap.png")
    {
        IO::Normalize(pPath, path);

        pMapOptions.mapInFile = true; // rendering is for an image file, not for on-screen display
        pMapOptions.transparentBackground = transparentBackground;
        pMapOptions.backgroundColor = backgroundColor;
        pMapOptions.fileFormat = format;

        pMapOptions.nbTiles = nbSplitParts;

        // getting the list of layers to save
        pMapOptions.layers = layers; // at the beginning, there is at least one layer
        pMapOptions.layers.unique();
        pMapOptions.layers.sort();
    }

    virtual ~JobExportMap()
    {
    }

protected:
    virtual bool executeTask()
    {
        // Logs
        logs.notice() << "Saving the map...";
        logs.info() << "  Destination: " << pPath;

        // The main form
        Forms::ApplWnd& mainFrm = *Forms::ApplWnd::Instance();
        // The map
        auto& map = *mainFrm.map();

        map.saveToImageFile(pPath, pMapOptions);
        // The task is complete
        return true;
    }

private:
    String pPath;
    /*bool pUseBackgroundColor;
    wxColour pBackgroundColor;
    bool pSplit;
    int pNbSplitParts;
    int pFormat;
    */
    Antares::Map::MapRenderOptions pMapOptions;

}; // class JobExportMap

} // anonymous namespace

bool CheckIfInsideAStudyFolder(const AnyString& path, bool quiet)
{
    String location;
    String title;
    if (!Data::Study::IsInsideStudyFolder(path, location, title))
        return false;

    if (!quiet)
    {
        auto& mainFrm = *Forms::ApplWnd::Instance();
        Window::Message message(&mainFrm,
                                wxT("Save As"),
                                wxT("Impossible to save the study"),
                                wxString() << wxT("Impossible to save the study into another one. "
                                                  "That may lead to data loss.\n\nTitle : ")
                                           << wxStringFromUTF8(title) << wxT("\nLocation : ")
                                           << wxStringFromUTF8(location),
                                "images/misc/error.png");
        message.add(Window::Message::btnCancel);
        message.showModal();
    }
    return true;
}

void MemoryFlushBeginUpdate()
{
    ++gMemoryFlushRefCount;
}

void MemoryFlushEndUpdate()
{
    assert(gMemoryFlushRefCount > 0);
    --gMemoryFlushRefCount;
}

bool CanPerformMemoryFlush()
{
    return !gMemoryFlushRefCount;
}

uint64_t StudyInMemoryRevisionID()
{
    return gInMemoryRevisionIncrement;
}

void MarkTheStudyAsModified(const Data::Study::Ptr& study)
{
    if (!(!study) and study == GetCurrentStudy())
        MarkTheStudyAsModified();
}
void MarkTheStudyAsModified()
{
    auto study = GetCurrentStudy();
    if (!(!study))
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wvolatile"
        ++gInMemoryRevisionIncrement;
#pragma GCC diagnostic pop
#pragma GCC diagnostic pop
        if (!gStudyHasBeenModified)
        {
            gStudyHasBeenModified = true;
            logs.info() << "The study has been modified.";
            wxWindow* wnd = wxWindow::FindFocus();

            Forms::ApplWnd& mainFrm = *Forms::ApplWnd::Instance();
            mainFrm.title(wxStringFromUTF8(study->header.caption));
            mainFrm.Refresh();

            OnStudyChanged(*study);
            if (wnd)
                wnd->SetFocus();
        }
    }
}

void ResetTheModifierState(bool v)
{
    auto study = GetCurrentStudy();
    gInMemoryRevisionIncrement = 0;
    gStudyHasBeenModified = v;
    if (!study)
        Forms::ApplWnd::Instance()->title();
    else
        Forms::ApplWnd::Instance()->title(wxStringFromUTF8(study->header.caption));
}

bool StudyHasBeenModified()
{
    return gStudyHasBeenModified;
}

bool CloseTheStudy(bool updateGUI)
{
    // alias to the main form
    auto& mainFrm = *Forms::ApplWnd::Instance();

    // Close any opened windows
    Component::Spotlight::FrameClose();
    // Change the focus - to avoid race condition with wxGrid or any other
    // component
    mainFrm.SetFocus();

    // keeping a reference to the current study to avoid unwanted
    // deletion
    auto study = GetCurrentStudy();

    if (!study or !Forms::ApplWnd::Instance())
        return false;

    GUILocker locker;
    logs.info();
    logs.notice() << "Closing the study...";
    WIP::Locker wip;

    // Main form
    Forms::Disabler<Forms::ApplWnd> disabler(mainFrm);

    mainFrm.hideAllComponentsRelatedToTheStudy();

    // Remove all data that belongs to the inspector
    Window::Inspector::Destroy();

    ResetTheModifierState(false);

    // Lazy creation of all visual components (just in case)
    // But it should never happen
    mainFrm.createAllComponentsNeededByTheMainNotebook();

    // Logs
    mainFrm.refreshStudyLogs();

    // Map
    // The map must be detached first from the study to prevent changes
    mainFrm.map()->detachStudy(false);
    // Clear all controls
    mainFrm.map()->clear();

    // Broadcast the new !
    // All controls with information from the current study must reset
    // their variables
    OnStudyClosed();

    // Deferencing pointer - We have to keep the data stored by
    // the study valid for a bit more longer
    SetCurrentStudy(nullptr);

    // Notice that the simulation'settings have changed
    SystemParameterHaveChanged = true;

    ResetLastOpenedFilepath();

    // Clear infos about outputs for the study
    ListOfOutputsForTheCurrentStudy.clear();

    logs.info() << "The study is closed.";

    // The study will be delete the next time
    // mainFrm.data()->addStudyToPendingDelete(study);

    // Update the GUI accordingly
    if (updateGUI)
        mainFrm.requestUpdateGUIAfterStudyIO(false);

    Map::Control::newUID = 1;

    return true;
}

void NewStudy()
{
    GUILocker locker;
    WIP::Locker wip;

    // Main form
    auto& mainFrm = *Forms::ApplWnd::Instance();

    Forms::Disabler<Forms::ApplWnd> disabler(mainFrm);

    // closing the startup wizard if already opened
    Window::StartupWizard::Close();

    // Lazy creation of all visual components
    mainFrm.createAllComponentsNeededByTheMainNotebook();

    // Make sure there is no remaining study
    CloseTheStudy(false);

    // Creating a new one
    logs.info();
    logs.checkpoint() << "Creating a new study";

    auto study = std::make_shared<Data::Study>();
    study->createAsNew();

    // reset the new current study
    SetCurrentStudy(study);

    ListOfOutputsForTheCurrentStudy.clear();

    StudyRefreshCalendar();

    // Reconnect the logs
    mainFrm.connectLogCallback();

    // User notes
    mainFrm.loadUserNotes();
    mainFrm.loadSets();
    mainFrm.selectSystem();
    SystemParameterHaveChanged = true;

    ResetLastOpenedFilepath();

    ResetTheModifierState(true);

    mainFrm.requestUpdateGUIAfterStudyIO(true);

    OnStudyLoaded();
    OnStudyChanged(*study);
}

SaveResult SaveStudy()
{
    auto studyptr = GetCurrentStudy();
    if (!studyptr or !Forms::ApplWnd::Instance())
        return svsCancel;

    GUILocker locker;
    // The currently opened study
    auto& study = *studyptr;
    // The main form
    auto& mainFrm = *Forms::ApplWnd::Instance();

    if (study.parameters.readonly)
    {
        Window::Message message(&mainFrm,
                                wxT("Save changes"),
                                wxT("Impossible to save the study"),
                                wxT("The study is read-only. Use 'Save as' instead."),
                                "images/misc/error.png");
        message.add(Window::Message::btnCancel);
        message.showModal();
        return svsCancel;
    }

    // User notes
    // Copy the User notes into the struct `Study`
    mainFrm.saveUserNotes();

    // SaveAs required
    // Especially when the study is readonly (by lock or written in the general data)
    if (study.folder.empty() || study.readonly())
        return Window::SaveAs::Execute(&mainFrm, studyptr);

    // We want to invalidate the whole study when upgrading
    bool shouldInvalidateStudy = false;

    // Detection for old format
    if (study.header.version != Data::StudyVersion::latest())
    {
        Window::Message message(
          &mainFrm,
          wxT("Study Upgrade"),
          wxT("The study was saved with a previous version of Antares"),
          wxString() << wxT("You can choose either to upgrade the study folder or to save it\n")
                     << wxT("into a new folder.\n\nCurrent version of Antares : ")
                     << Data::StudyVersion::latest().toString()
                     << wxT("\nFormat version of the study : ")
                     << study.header.version.toString(),
          "images/misc/save.png");
        message.add(Window::Message::btnUpgrade);
        message.add(Window::Message::btnSaveAs, false, 15);
        message.add(Window::Message::btnCancel, true);
        switch (message.showModal())
        {
        case Window::Message::btnUpgrade:
            shouldInvalidateStudy = true;
            break;
        case Window::Message::btnSaveAs:
            return Window::SaveAs::Execute(&mainFrm, studyptr);
        default:
            return svsCancel;
        }
    }

    WIP::Locker wip;
    Forms::Disabler<Forms::ApplWnd> disabler(mainFrm);
    OnStudyBeginUpdate();

    // Reset the entries
    Menu::AddRecentFile(mainFrm.menuRecentFiles(),
                        wxStringFromUTF8(study.header.caption),
                        wxStringFromUTF8(study.folder.string()));

    mainFrm.SetStatusText(wxString() << wxT("  Saving ") << wxStringFromUTF8(study.folder.string()));

    // Save the study
    auto* job = new JobSaveStudy(studyptr, study.folder.string());
    if (shouldInvalidateStudy)
        job->shouldInvalidateStudy();
    job->run();
    job->Destroy();

    // Lock the window to prevent flickering
    wxWindowUpdateLocker updater(&mainFrm);

    // The study is not modified anymore
    ResetTheModifierState(false);

    // The binding constraints data must be reloaded since the current
    // code is not able to dynamically reload it by itself
    study.ensureDataAreLoadedForAllBindingConstraints();
    // Reload runtime info about the study (Paranoid, should not be required)
    if (study.uiinfo)
        study.uiinfo->reloadAll();

    // GUIFlagInvalidateAreas = true;

    Menu::AddRecentFile(mainFrm.menuRecentFiles(),
                        wxStringFromUTF8(study.header.caption),
                        wxStringFromUTF8(study.folder.string()));
    // Rebuild the menu
    Menu::RebuildRecentFiles(mainFrm.menuRecentFiles());

    gLastOpenedStudyFolder = wxStringFromUTF8(study.folder.string());

    RefreshListOfOutputsForTheCurrentStudy();

    OnStudyChanged(study);
    mainFrm.refreshMenuInput();
    mainFrm.refreshMenuOptions(studyptr);
    mainFrm.requestUpdateGUIAfterStudyIO(true);

    OnStudySaved();
    OnStudyEndUpdate();

    return svsSaved;
}

SaveResult SaveStudyAs(const String& path, bool copyoutput, bool copyuserdata, bool copylogs)
{
    if (!CurrentStudyIsValid() || path.empty())
        return svsCancel;

    // alias to the current study
    auto study = GetCurrentStudy();
    // alias to the main form
    auto& mainFrm = *Forms::ApplWnd::Instance();

    // Normalizing the target path
    String newPath;
    IO::Normalize(newPath, path);

    if (!study->folder.empty())
    {
        String oldP = study->folder.string();
        String newP = newPath;
        newP.removeTrailingSlash();
        oldP.removeTrailingSlash();
        if (newP == oldP)
            return SaveStudy();
    }

    // Ok ! We're good to go !
    GUILocker locker;
    logs.notice() << "Save the study as...";
    WIP::Locker wip;

    // User notes
    mainFrm.saveUserNotes();

    Forms::Disabler<Forms::ApplWnd> disabler(mainFrm);

    {
        StudyUpdateLocker studylocker;

        mainFrm.SetStatusText(wxString() << wxT("  Saving ") << wxStringFromUTF8(newPath));

        // Save the study as (in background)
        auto* job = new JobSaveStudy(study, newPath, copyoutput, copyuserdata, copylogs);
        job->saveAs(true);
        job->run();
        job->Destroy();

        // Lock the window to prevent flickering
        wxWindowUpdateLocker updater(&mainFrm);

        // The study is not modified anymore
        ResetTheModifierState(false);

        // The binding constraints data must be reloaded since the current
        // code is not able to dynamically reload it by itself
        study->ensureDataAreLoadedForAllBindingConstraints();
        // Reload runtime info about the study (Paranoid, should not be required)
        if (study->uiinfo)
            study->uiinfo->reloadAll();

        // GUIFlagInvalidateAreas = true;
        OnStudySavedAs();
    }

    finalizeSaveExport(study, mainFrm);
    return svsSaved;
}

SaveResult ExportMap(const Yuni::String& path,
                     bool transparentBackground,
                     const wxColour& backgroundColor,
                     const std::list<uint16_t>& layers,
                     int nbSplitParts,
                     Antares::Map::mapImageFormat format)
{
    if (!CurrentStudyIsValid() || path.empty())
        return svsCancel;

    // alias to the current study
    auto study = GetCurrentStudy();
    // alias to the main form
    auto& mainFrm = *Forms::ApplWnd::Instance();

    // Normalizing the target path
    String newPath;
    IO::Normalize(newPath, path);

    // Ok ! We're good to go !
    GUILocker locker;
    logs.notice() << "Exporting map...";
    WIP::Locker wip;

    Forms::Disabler<Forms::ApplWnd> disabler(mainFrm);

    {
        StudyUpdateLocker studylocker;

        mainFrm.SetStatusText(wxString() << wxT("  Exporting map ") << wxStringFromUTF8(newPath));

        // Exporting map (in background)
        auto* job = new JobExportMap(
          path, transparentBackground, backgroundColor, layers, nbSplitParts, format);
        job->run();
        job->Destroy();

        // Lock the window to prevent flickering
        wxWindowUpdateLocker updater(&mainFrm);

        // The study is not modified anymore
        ResetTheModifierState(false);

        // The binding constraints data must be reloaded since the current
        // code is not able to dynamically reload it by itself
        study->ensureDataAreLoadedForAllBindingConstraints();
        // Reload runtime info about the study (Paranoid, should not be required)
        if (study->uiinfo)
            study->uiinfo->reloadAll();

        // GUIFlagInvalidateAreas = true;
        OnStudySavedAs();
    }

    finalizeSaveExport(study, mainFrm);
    return svsSaved;
}

void UpdateGUIFromStudyState()
{
    auto* mainform = Forms::ApplWnd::Instance();
    if (mainform)
        mainform->requestUpdateGUIAfterStudyIO(CurrentStudyIsValid());
}

void OpenStudyFromFolder(wxString folder)
{
    // It is important on Windows to not have the final backslash
    if ('\\' == folder.Last() || '/' == folder.Last())
        folder.RemoveLast();

    // Getting the version of the study
    String studyfolder;
    wxStringToString(folder, studyfolder);
    auto version = Data::StudyHeader::tryToFindTheVersion(studyfolder);

    if (version == Data::StudyVersion::unknown())
    {
        logs.error() << studyfolder << ": it does not seem a valid study";
        return;
    }

    if (version > Data::StudyVersion::latest())
    {
        logs.error() << "A more recent version of Antares is required to open `" << studyfolder
            << "`  (" << version.toString() << ')';
        return;
    }

    // Lazy creation of all visual components
    GUILocker locker;
    auto& mainFrm = *Forms::ApplWnd::Instance();
    Forms::Disabler<Forms::ApplWnd> disabler(mainFrm);

    // closing the startup wizard if already opened
    Window::StartupWizard::Close();
    // create components if required
    mainFrm.createAllComponentsNeededByTheMainNotebook();

    WIP::Locker wip;

    // disabling the logs
    mainFrm.beginUpdateLogs();

    CloseTheStudy();

    mainFrm.SetStatusText(wxString() << wxT("  Loading ") << folder);
    mainFrm.Refresh();

    // Opening the study in another thread
    auto* job = new JobOpenStudy(folder);
    job->run();
    job->Destroy();
    ResetTheModifierState(false);

    // enabling the logs
    mainFrm.endUpdateLogs();

    if (CurrentStudyIsValid())
    {
        auto study = GetCurrentStudy();
        if (!study->folder.empty())
        {
            Menu::AddRecentFile(mainFrm.menuRecentFiles(),
                                wxStringFromUTF8(study->header.caption),
                                wxStringFromUTF8(study->folder.string()));
        }
    }
    // Lock the window to prevent flickering
    {
        // GUIFlagInvalidateAreas = true;
        wxWindowUpdateLocker updater(&mainFrm);
        OnStudyLoaded();
        auto studyptr = GetCurrentStudy();
        if (!(!studyptr)) // should never be null
            OnStudyChanged(*studyptr);
    }

    // User notes
    mainFrm.selectSystem();

    mainFrm.requestUpdateGUIAfterStudyIO(true);
}

void StudyRefreshCalendar()
{
    auto studyptr = GetCurrentStudy();
    if (studyptr)
    {
        studyptr->calendar.reset({studyptr->parameters.dayOfThe1stJanuary,
                                  studyptr->parameters.firstWeekday,
                                  studyptr->parameters.firstMonthInYear,
                                  studyptr->parameters.leapYear});
        studyptr->calendarOutput.reset({studyptr->parameters.dayOfThe1stJanuary,
                                        studyptr->parameters.firstWeekday,
                                        studyptr->parameters.firstMonthInYear,
                                        studyptr->parameters.leapYear});
    }
}

void RunSimulationOnTheStudy(Data::Study::Ptr study,
                             const String& simuName,
                             const String& commentFile,
                             bool ignoreWarnings,
                             Solver::Feature features,
                             bool preproOnly,
                             const std::string& solverName)
{
    if (!study) // A valid study would be better
    {
        logs.fatal() << "Internal error: Please provide a valid study";
        return;
    }
    if (!Forms::ApplWnd::Instance())
        return;
    if (IsGUIAboutToQuit())
        return;

    GUILocker locker;
    auto& mainFrm = *Forms::ApplWnd::Instance();

    bool result = false;
    wxTimeSpan timeSpan;
    String tmp;

    WIP::Locker wip;
    Forms::Disabler<Forms::ApplWnd> disabler(mainFrm);

    {
        // Logs
        mainFrm.SetStatusText(wxString() << wxT("  Running ") << wxStringFromUTF8(study->folder.string()));
        logs.info();
        logs.checkpoint() << "Launching the simulation";

        // Where is our solver ?
        String solverLocation;
        if (!Solver::FindLocation(solverLocation))
        {
            logs.error() << "Impossible to find the program `antares-solver`.";
            return;
        }
        logs.info() << "  :: found solver: " << solverLocation;
        logs.info() << "  :: study folder: " << study->folder;
        logs.info();

        // Resetting statistics
        Statistics::Reset();

        // The process utility
        auto* exec = new Toolbox::Process::Execute();
        exec->title(wxT("Simulation"));

        if (preproOnly)
        {
            exec->subTitle(wxT("Running timeseries generators"));
        }
        else
        {
            if (study->parameters.nbYears == 1)
            {
                exec->subTitle(wxT("Running for 1 Monte Carlo year"));
            }
            else
            {
                exec->subTitle(wxString() << wxT("Running for ") << study->parameters.nbYears
                                          << wxT(" Monte Carlo years"));
            }
        }
        exec->icon("images/32x32/run.png");

        // The command line
        {
            // The full command line
            String cmd;

            // Priority for the process
            // On Windows, it is done by the process itself (see solver/misc/process-priority.cpp)
            if (System::unix)
            {
                if (System::CPU::Count() <= 2) // nice on unixes is good
                    cmd << "nice ";
            }

            // binary
            AppendWithQuotes(cmd, solverLocation);

            // Name of the simulation
            if (!simuName.empty())
            {
                cmd << ' ';
                tmp.clear() << "--name=" << simuName;
                AppendWithQuotes(cmd, tmp);
            }

            // enable the progress meter
            cmd << " --progress";

            // Comments
            if (!commentFile.empty())
            {
                cmd << ' ';
                tmp.clear() << "--comment-file=" << commentFile;
                AppendWithQuotes(cmd, tmp);
            }

            // Force
            if (ignoreWarnings)
                cmd << " --force";

            // Prepro only
            if (preproOnly)
                cmd << " --generators-only";

            cmd << ' ';
            // The input data
            String s = study->folder.string();
            AppendWithQuotes(cmd, s);

            // Parallel mode chosen ?
            if (features == Solver::parallel)
                cmd << " --parallel";

            // add solver name for ortools
            if (!solverName.empty())
            {
                cmd << " --solver=" << solverName;
            }

            // Go go go !
            logs.debug() << "running " << cmd;
            wxString shellCmd = wxStringFromUTF8(cmd);
            exec->command(shellCmd);
        }

        OnStudyBeginUpdate();

        // running
        {
            logs.info() << "running...";
            // Getting when the process was launched
            const wxDateTime startTime = wxDateTime::Now();
            // Running the simulation - it may take some time
            result = exec->run();
            // How long took the simulation ?
            timeSpan = wxDateTime::Now() - startTime;

            String duration;
            wxStringToString(timeSpan.Format(), duration);
            if (result)
                logs.info() << "The simulation is over : " << duration;
            else
                logs.info() << "The simulation has been canceled : " << duration;
        }

        // Releasing
        delete exec;

        // Lock the window to prevent flickering
        wxWindowUpdateLocker updater(&mainFrm);
        // Refreshing the output
        RefreshListOfOutputsForTheCurrentStudy();

        if (result)
            logs.info() << "The simulation has ended.";
        logs.info(); // space, for beauty

        // Reset the status bar
        mainFrm.resetDefaultStatusBarText();

        mainFrm.requestUpdateGUIAfterStudyIO(true);
        OnStudyEndUpdate();

        auto studyptr = GetCurrentStudy();
        if (!(!studyptr)) // should never be null
            OnStudyChanged(*studyptr);
    }

    if (result)
    {
        // Delaying the dialog to show to the user that the simulation is over
        Bind<void()> callback;
        callback.bind(&TheSimulationIsComplete, timeSpan.Format());
        Dispatcher::GUI::Post(callback);
    }
}

void RefreshListOfOutputsForTheCurrentStudy()
{
    ListOfOutputsForTheCurrentStudy.clear();
    if (IsGUIAboutToQuit())
        return;

    if (auto study = GetCurrentStudy(); CurrentStudyIsValid())
    {
        Data::Output::RetrieveListFromStudy(ListOfOutputsForTheCurrentStudy, *study);
    }
    else
    {
        ListOfOutputsForTheCurrentStudy.clear();
    }

    auto* mainfrm = Forms::ApplWnd::Instance();
    if (mainfrm)
        mainfrm->refreshMenuOutput();
}

bool StudyRenameArea(Data::Area* area, const AnyString& newname, Data::Study* study)
{
    if (!area || newname.empty())
        return false;

    auto currentstudy = GetCurrentStudy();
    if (!study)
        study = currentstudy.get();
    if (!study)
        return false;

    if (study == currentstudy.get())
    {
        wxBusyInfo wait(wxT("renaming area..."));
        OnStudyBeginUpdate();
        if (study->areaRename(area, newname))
        {
            auto& mainFrm = *Antares::Forms::ApplWnd::Instance();
            auto& map = *mainFrm.map();

            map.renameNodeFromArea(area);
            map.refresh();

            // triggering events
            OnStudyAreaRename(area);
            OnStudyEndUpdate();
            return true;
        }
        OnStudyEndUpdate();

        String beautifyname;
        BeautifyName(beautifyname, newname);
        if (beautifyname.empty())
            logs.error() << "impossible to rename the area '" << area->name
                         << "' with an empty name";
        else
        {
            logs.error() << "impossible to rename the area '" << area->name << "' to '"
                         << beautifyname << "'";
        }
    }
    else
    {
        if (study->areaRename(area, newname))
            return true;
    }
    return false;
}

static std::shared_ptr<Data::Study> CURRENT_STUDY;

void SetCurrentStudy(std::shared_ptr<Data::Study> study)
{
    CURRENT_STUDY = study;
}

std::shared_ptr<Data::Study> GetCurrentStudy()
{
    return CURRENT_STUDY;
}

bool CurrentStudyIsValid()
{
    return !(!CURRENT_STUDY);
}

} // namespace Antares
