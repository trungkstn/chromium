// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/panels/base_panel_browser_test.h"
#include "chrome/browser/ui/panels/detached_panel_strip.h"
#include "chrome/browser/ui/panels/docked_panel_strip.h"
#include "chrome/browser/ui/panels/overflow_panel_strip.h"
#include "chrome/browser/ui/panels/native_panel.h"
#include "chrome/browser/ui/panels/panel.h"
#include "chrome/browser/ui/panels/panel_drag_controller.h"
#include "chrome/browser/ui/panels/panel_manager.h"

class PanelDragBrowserTest : public BasePanelBrowserTest {
 public:
  PanelDragBrowserTest() : BasePanelBrowserTest() {
  }

  virtual ~PanelDragBrowserTest() {
  }

  virtual void SetUpOnMainThread() OVERRIDE {
    BasePanelBrowserTest::SetUpOnMainThread();

    // All the tests here assume 800x600 work area. Do the check now.
    DCHECK(PanelManager::GetInstance()->work_area().width() == 800);
    DCHECK(PanelManager::GetInstance()->work_area().height() == 600);
  }

  // Drag |panel| from its origin by the offset |delta|.
  void DragPanelByDelta(Panel* panel, const gfx::Point& delta) {
    scoped_ptr<NativePanelTesting> panel_testing(
        NativePanelTesting::Create(panel->native_panel()));
    gfx::Point mouse_location(panel->GetBounds().origin());
    panel_testing->PressLeftMouseButtonTitlebar(mouse_location);
    panel_testing->DragTitlebar(mouse_location.Add(delta));
    panel_testing->FinishDragTitlebar();
  }

  // Drag |panel| from its origin to |new_mouse_location|.
  void DragPanelToMouseLocation(Panel* panel,
                                const gfx::Point& new_mouse_location) {
    scoped_ptr<NativePanelTesting> panel_testing(
        NativePanelTesting::Create(panel->native_panel()));
    gfx::Point mouse_location(panel->GetBounds().origin());
    panel_testing->PressLeftMouseButtonTitlebar(panel->GetBounds().origin());
    panel_testing->DragTitlebar(new_mouse_location);
    panel_testing->FinishDragTitlebar();
  }

  static gfx::Point GetDragDeltaToRemainDocked() {
    return gfx::Point(
        -5,
        -(PanelDragController::GetDetachDockedPanelThreshold() / 2));
  }

  static gfx::Point GetDragDeltaToDetach() {
    return gfx::Point(
        -20,
        -(PanelDragController::GetDetachDockedPanelThreshold() + 20));
  }

  static gfx::Point GetDragDeltaToRemainDetached(Panel* panel) {
    int distance = panel->manager()->docked_strip()->display_area().bottom() -
                   panel->GetBounds().bottom();
    return gfx::Point(
        -5,
        distance - PanelDragController::GetDockDetachedPanelThreshold() * 2);
  }

  static gfx::Point GetDragDeltaToAttach(Panel* panel) {
    int distance = panel->manager()->docked_strip()->display_area().bottom() -
                   panel->GetBounds().bottom();
    return gfx::Point(
        -20,
        distance - PanelDragController::GetDockDetachedPanelThreshold() / 2);
  }
};

IN_PROC_BROWSER_TEST_F(PanelDragBrowserTest, NotDraggable) {
  Panel* panel = CreatePanel("panel");
  // This is used to simulate making a docked panel not draggable.
  panel->set_has_temporary_layout(true);
  Panel* panel2 = CreatePanel("panel2");

  scoped_ptr<NativePanelTesting> panel_testing(
      NativePanelTesting::Create(panel->native_panel()));
  gfx::Rect bounds = panel->GetBounds();
  gfx::Point mouse_location = bounds.origin();
  panel_testing->PressLeftMouseButtonTitlebar(mouse_location);
  EXPECT_EQ(bounds.x(), panel->GetBounds().x());
  mouse_location.Offset(-50, 10);
  panel_testing->DragTitlebar(mouse_location);
  EXPECT_EQ(bounds.x(), panel->GetBounds().x());
  panel_testing->FinishDragTitlebar();
  EXPECT_EQ(bounds.x(), panel->GetBounds().x());

  // Reset the simulation hack so that the panel can be closed correctly.
  panel->set_has_temporary_layout(false);
  panel->Close();
  panel2->Close();
}

IN_PROC_BROWSER_TEST_F(PanelDragBrowserTest, DragOneDockedPanel) {
  static const int big_delta_x = 70;
  static const int big_delta_y = 30;  // Do not exceed the threshold to detach.

  Panel* panel = CreateDockedPanel("1", gfx::Rect(0, 0, 100, 100));
  scoped_ptr<NativePanelTesting> panel_testing(
      NativePanelTesting::Create(panel->native_panel()));
  gfx::Rect panel_old_bounds = panel->GetBounds();

  // Drag left.
  gfx::Point mouse_location = panel_old_bounds.origin();
  panel_testing->PressLeftMouseButtonTitlebar(mouse_location);
  EXPECT_EQ(panel_old_bounds, panel->GetBounds());

  mouse_location.Offset(-big_delta_x, 0);
  panel_testing->DragTitlebar(mouse_location);
  gfx::Rect panel_new_bounds = panel_old_bounds;
  panel_new_bounds.Offset(-big_delta_x, 0);
  EXPECT_EQ(panel_new_bounds, panel->GetBounds());

  panel_testing->FinishDragTitlebar();
  EXPECT_EQ(panel_old_bounds, panel->GetBounds());

  // Drag left and cancel.
  mouse_location = panel_old_bounds.origin();
  panel_testing->PressLeftMouseButtonTitlebar(mouse_location);
  EXPECT_EQ(panel_old_bounds, panel->GetBounds());

  mouse_location.Offset(-big_delta_x, 0);
  panel_testing->DragTitlebar(mouse_location);
  panel_new_bounds = panel_old_bounds;
  panel_new_bounds.Offset(-big_delta_x, 0);
  EXPECT_EQ(panel_new_bounds, panel->GetBounds());

  panel_testing->CancelDragTitlebar();
  EXPECT_EQ(panel_old_bounds, panel->GetBounds());

  // Drag right.
  mouse_location = panel_old_bounds.origin();
  panel_testing->PressLeftMouseButtonTitlebar(mouse_location);
  EXPECT_EQ(panel_old_bounds, panel->GetBounds());

  mouse_location.Offset(big_delta_x, 0);
  panel_testing->DragTitlebar(mouse_location);
  panel_new_bounds = panel_old_bounds;
  panel_new_bounds.Offset(big_delta_x, 0);
  EXPECT_EQ(panel_new_bounds, panel->GetBounds());

  panel_testing->FinishDragTitlebar();
  EXPECT_EQ(panel_old_bounds, panel->GetBounds());

  // Drag right and up.  Expect no vertical movement.
  mouse_location = panel_old_bounds.origin();
  panel_testing->PressLeftMouseButtonTitlebar(mouse_location);
  EXPECT_EQ(panel_old_bounds, panel->GetBounds());

  mouse_location.Offset(big_delta_x, big_delta_y);
  panel_testing->DragTitlebar(mouse_location);
  panel_new_bounds = panel_old_bounds;
  panel_new_bounds.Offset(big_delta_x, 0);
  EXPECT_EQ(panel_new_bounds, panel->GetBounds());

  panel_testing->FinishDragTitlebar();
  EXPECT_EQ(panel_old_bounds, panel->GetBounds());

  // Drag up.  Expect no movement on drag.
  mouse_location = panel_old_bounds.origin();
  panel_testing->PressLeftMouseButtonTitlebar(mouse_location);
  EXPECT_EQ(panel_old_bounds, panel->GetBounds());

  mouse_location.Offset(0, -big_delta_y);
  panel_testing->DragTitlebar(mouse_location);
  EXPECT_EQ(panel_old_bounds, panel->GetBounds());

  panel_testing->FinishDragTitlebar();
  EXPECT_EQ(panel_old_bounds, panel->GetBounds());

  // Drag down.  Expect no movement on drag.
  mouse_location = panel_old_bounds.origin();
  panel_testing->PressLeftMouseButtonTitlebar(mouse_location);
  EXPECT_EQ(panel_old_bounds, panel->GetBounds());

  mouse_location.Offset(0, big_delta_y);
  panel_testing->DragTitlebar(mouse_location);
  EXPECT_EQ(panel_old_bounds, panel->GetBounds());

  panel_testing->FinishDragTitlebar();
  EXPECT_EQ(panel_old_bounds, panel->GetBounds());

  PanelManager::GetInstance()->CloseAll();
}

IN_PROC_BROWSER_TEST_F(PanelDragBrowserTest, DragTwoDockedPanels) {
  static const gfx::Point small_delta(10, 0);

  Panel* panel1 = CreateDockedPanel("1", gfx::Rect(0, 0, 100, 100));
  Panel* panel2 = CreateDockedPanel("2", gfx::Rect(0, 0, 100, 100));
  scoped_ptr<NativePanelTesting> panel1_testing(
      NativePanelTesting::Create(panel1->native_panel()));
  scoped_ptr<NativePanelTesting> panel2_testing(
      NativePanelTesting::Create(panel2->native_panel()));
  gfx::Point position1 = panel1->GetBounds().origin();
  gfx::Point position2 = panel2->GetBounds().origin();

  // Drag right panel towards left with small delta.
  // Expect no shuffle: P1 P2
  gfx::Point mouse_location = position1;
  panel1_testing->PressLeftMouseButtonTitlebar(mouse_location);
  EXPECT_EQ(position1, panel1->GetBounds().origin());
  EXPECT_EQ(position2, panel2->GetBounds().origin());

  mouse_location = mouse_location.Subtract(small_delta);
  panel1_testing->DragTitlebar(mouse_location);
  EXPECT_EQ(mouse_location, panel1->GetBounds().origin());
  EXPECT_EQ(position2, panel2->GetBounds().origin());

  panel1_testing->FinishDragTitlebar();
  EXPECT_EQ(position1, panel1->GetBounds().origin());
  EXPECT_EQ(position2, panel2->GetBounds().origin());

  // Drag right panel towards left with big delta.
  // Expect shuffle: P2 P1
  mouse_location = position1;
  panel1_testing->PressLeftMouseButtonTitlebar(mouse_location);
  EXPECT_EQ(position1, panel1->GetBounds().origin());
  EXPECT_EQ(position2, panel2->GetBounds().origin());

  mouse_location = position2.Add(gfx::Point(1, 0));
  panel1_testing->DragTitlebar(mouse_location);
  EXPECT_EQ(mouse_location, panel1->GetBounds().origin());
  EXPECT_EQ(position1, panel2->GetBounds().origin());

  panel1_testing->FinishDragTitlebar();
  EXPECT_EQ(position2, panel1->GetBounds().origin());
  EXPECT_EQ(position1, panel2->GetBounds().origin());

  // Drag left panel towards right with small delta.
  // Expect no shuffle: P2 P1
  mouse_location = position2;
  panel1_testing->PressLeftMouseButtonTitlebar(mouse_location);
  EXPECT_EQ(position2, panel1->GetBounds().origin());
  EXPECT_EQ(position1, panel2->GetBounds().origin());

  mouse_location = mouse_location.Add(small_delta);
  panel1_testing->DragTitlebar(mouse_location);
  EXPECT_EQ(mouse_location, panel1->GetBounds().origin());
  EXPECT_EQ(position1, panel2->GetBounds().origin());

  panel1_testing->FinishDragTitlebar();
  EXPECT_EQ(position2, panel1->GetBounds().origin());
  EXPECT_EQ(position1, panel2->GetBounds().origin());

  // Drag left panel towards right with big delta.
  // Expect shuffle: P1 P2
  mouse_location = position2;
  panel1_testing->PressLeftMouseButtonTitlebar(mouse_location);
  EXPECT_EQ(position2, panel1->GetBounds().origin());
  EXPECT_EQ(position1, panel2->GetBounds().origin());

  mouse_location = position1.Add(gfx::Point(1, 0));
  panel1_testing->DragTitlebar(mouse_location);
  EXPECT_EQ(mouse_location, panel1->GetBounds().origin());
  EXPECT_EQ(position2, panel2->GetBounds().origin());

  panel1_testing->FinishDragTitlebar();
  EXPECT_EQ(position1, panel1->GetBounds().origin());
  EXPECT_EQ(position2, panel2->GetBounds().origin());

  // Drag right panel towards left with big delta and then cancel the drag.
  // Expect shuffle after drag:   P2 P1
  // Expect shuffle after cancel: P1 P2
  mouse_location = position1;
  panel1_testing->PressLeftMouseButtonTitlebar(mouse_location);
  EXPECT_EQ(position1, panel1->GetBounds().origin());
  EXPECT_EQ(position2, panel2->GetBounds().origin());

  mouse_location = position2.Add(gfx::Point(1, 0));
  panel1_testing->DragTitlebar(mouse_location);
  EXPECT_EQ(mouse_location, panel1->GetBounds().origin());
  EXPECT_EQ(position1, panel2->GetBounds().origin());

  panel1_testing->CancelDragTitlebar();
  EXPECT_EQ(position1, panel1->GetBounds().origin());
  EXPECT_EQ(position2, panel2->GetBounds().origin());

  PanelManager::GetInstance()->CloseAll();
}

IN_PROC_BROWSER_TEST_F(PanelDragBrowserTest, DragThreeDockedPanels) {
  Panel* panel1 = CreateDockedPanel("1", gfx::Rect(0, 0, 100, 100));
  Panel* panel2 = CreateDockedPanel("2", gfx::Rect(0, 0, 100, 100));
  Panel* panel3 = CreateDockedPanel("3", gfx::Rect(0, 0, 100, 100));
  scoped_ptr<NativePanelTesting> panel2_testing(
      NativePanelTesting::Create(panel2->native_panel()));
  scoped_ptr<NativePanelTesting> panel3_testing(
      NativePanelTesting::Create(panel3->native_panel()));
  gfx::Point position1 = panel1->GetBounds().origin();
  gfx::Point position2 = panel2->GetBounds().origin();
  gfx::Point position3 = panel3->GetBounds().origin();

  // Drag leftmost panel to become the rightmost in 2 drags. Each drag will
  // shuffle one panel.
  // Expect shuffle after 1st drag: P1 P3 P2
  // Expect shuffle after 2nd drag: P3 P1 P2
  gfx::Point mouse_location = position3;
  panel3_testing->PressLeftMouseButtonTitlebar(mouse_location);
  EXPECT_EQ(position1, panel1->GetBounds().origin());
  EXPECT_EQ(position2, panel2->GetBounds().origin());
  EXPECT_EQ(position3, panel3->GetBounds().origin());

  mouse_location = position2.Add(gfx::Point(1, 0));
  panel3_testing->DragTitlebar(mouse_location);
  EXPECT_EQ(position1, panel1->GetBounds().origin());
  EXPECT_EQ(position3, panel2->GetBounds().origin());
  EXPECT_EQ(mouse_location, panel3->GetBounds().origin());

  mouse_location = position1.Add(gfx::Point(1, 0));
  panel3_testing->DragTitlebar(mouse_location);
  EXPECT_EQ(position2, panel1->GetBounds().origin());
  EXPECT_EQ(position3, panel2->GetBounds().origin());
  EXPECT_EQ(mouse_location, panel3->GetBounds().origin());

  panel3_testing->FinishDragTitlebar();
  EXPECT_EQ(position2, panel1->GetBounds().origin());
  EXPECT_EQ(position3, panel2->GetBounds().origin());
  EXPECT_EQ(position1, panel3->GetBounds().origin());

  // Drag rightmost panel to become the leftmost in 2 drags and then cancel the
  // drag. Each drag will shuffle one panel and the cancellation will restore
  // all panels.
  // Expect shuffle after 1st drag: P1 P3 P2
  // Expect shuffle after 2nd drag: P1 P2 P3
  // Expect shuffle after cancel:   P3 P1 P2
  mouse_location = position1;
  panel3_testing->PressLeftMouseButtonTitlebar(mouse_location);
  EXPECT_EQ(position2, panel1->GetBounds().origin());
  EXPECT_EQ(position3, panel2->GetBounds().origin());
  EXPECT_EQ(position1, panel3->GetBounds().origin());

  mouse_location = position2.Add(gfx::Point(1, 0));
  panel3_testing->DragTitlebar(mouse_location);
  EXPECT_EQ(position1, panel1->GetBounds().origin());
  EXPECT_EQ(position3, panel2->GetBounds().origin());
  EXPECT_EQ(mouse_location, panel3->GetBounds().origin());

  mouse_location = position3.Add(gfx::Point(1, 0));
  panel3_testing->DragTitlebar(mouse_location);
  EXPECT_EQ(position1, panel1->GetBounds().origin());
  EXPECT_EQ(position2, panel2->GetBounds().origin());
  EXPECT_EQ(mouse_location, panel3->GetBounds().origin());

  panel3_testing->CancelDragTitlebar();
  EXPECT_EQ(position2, panel1->GetBounds().origin());
  EXPECT_EQ(position3, panel2->GetBounds().origin());
  EXPECT_EQ(position1, panel3->GetBounds().origin());

  // Drag leftmost panel to become the rightmost in a single drag. The drag will
  // shuffle 2 panels at a time.
  // Expect shuffle: P2 P3 P1
  mouse_location = position3;
  panel2_testing->PressLeftMouseButtonTitlebar(mouse_location);
  EXPECT_EQ(position2, panel1->GetBounds().origin());
  EXPECT_EQ(position3, panel2->GetBounds().origin());
  EXPECT_EQ(position1, panel3->GetBounds().origin());

  mouse_location = position1.Add(gfx::Point(1, 0));
  panel2_testing->DragTitlebar(mouse_location);
  EXPECT_EQ(position3, panel1->GetBounds().origin());
  EXPECT_EQ(mouse_location, panel2->GetBounds().origin());
  EXPECT_EQ(position2, panel3->GetBounds().origin());

  panel2_testing->FinishDragTitlebar();
  EXPECT_EQ(position3, panel1->GetBounds().origin());
  EXPECT_EQ(position1, panel2->GetBounds().origin());
  EXPECT_EQ(position2, panel3->GetBounds().origin());

  // Drag rightmost panel to become the leftmost in a single drag. The drag will
  // shuffle 2 panels at a time.
  // Expect shuffle: P3 P1 P2
  mouse_location = position1;
  panel2_testing->PressLeftMouseButtonTitlebar(mouse_location);
  EXPECT_EQ(position3, panel1->GetBounds().origin());
  EXPECT_EQ(position1, panel2->GetBounds().origin());
  EXPECT_EQ(position2, panel3->GetBounds().origin());

  mouse_location = position3.Add(gfx::Point(1, 0));
  panel2_testing->DragTitlebar(mouse_location);
  EXPECT_EQ(position2, panel1->GetBounds().origin());
  EXPECT_EQ(mouse_location, panel2->GetBounds().origin());
  EXPECT_EQ(position1, panel3->GetBounds().origin());

  panel2_testing->FinishDragTitlebar();
  EXPECT_EQ(position2, panel1->GetBounds().origin());
  EXPECT_EQ(position3, panel2->GetBounds().origin());
  EXPECT_EQ(position1, panel3->GetBounds().origin());

  // Drag rightmost panel to become the leftmost in a single drag and then
  // cancel the drag. The drag will shuffle 2 panels and the cancellation will
  // restore all panels.
  // Expect shuffle after drag:   P1 P2 P3
  // Expect shuffle after cancel: P3 P1 P2
  mouse_location = position1;
  panel3_testing->PressLeftMouseButtonTitlebar(mouse_location);
  EXPECT_EQ(position2, panel1->GetBounds().origin());
  EXPECT_EQ(position3, panel2->GetBounds().origin());
  EXPECT_EQ(position1, panel3->GetBounds().origin());

  mouse_location = position3.Add(gfx::Point(1, 0));
  panel3_testing->DragTitlebar(mouse_location);
  EXPECT_EQ(position1, panel1->GetBounds().origin());
  EXPECT_EQ(position2, panel2->GetBounds().origin());
  EXPECT_EQ(mouse_location, panel3->GetBounds().origin());

  panel3_testing->CancelDragTitlebar();
  EXPECT_EQ(position2, panel1->GetBounds().origin());
  EXPECT_EQ(position3, panel2->GetBounds().origin());
  EXPECT_EQ(position1, panel3->GetBounds().origin());

  PanelManager::GetInstance()->CloseAll();
}

IN_PROC_BROWSER_TEST_F(PanelDragBrowserTest, CloseDockedPanelOnDrag) {
  PanelManager* panel_manager = PanelManager::GetInstance();
  PanelDragController* drag_controller = panel_manager->drag_controller();
  DockedPanelStrip* docked_strip = panel_manager->docked_strip();

  // Create 4 docked panels.
  // We have:  P4  P3  P2  P1
  Panel* panel1 = CreatePanelWithBounds("Panel1", gfx::Rect(0, 0, 100, 100));
  Panel* panel2 = CreatePanelWithBounds("Panel2", gfx::Rect(0, 0, 100, 100));
  Panel* panel3 = CreatePanelWithBounds("Panel3", gfx::Rect(0, 0, 100, 100));
  Panel* panel4 = CreatePanelWithBounds("Panel4", gfx::Rect(0, 0, 100, 100));
  ASSERT_EQ(4, docked_strip->num_panels());

  scoped_ptr<NativePanelTesting> panel1_testing(
      NativePanelTesting::Create(panel1->native_panel()));
  gfx::Point position1 = panel1->GetBounds().origin();
  gfx::Point position2 = panel2->GetBounds().origin();
  gfx::Point position3 = panel3->GetBounds().origin();
  gfx::Point position4 = panel4->GetBounds().origin();

  // Test the scenario: drag a panel, close another panel, cancel the drag.
  {
    std::vector<Panel*> panels;
    gfx::Point panel1_new_position = position1;
    panel1_new_position.Offset(-500, 0);

    // Start dragging a panel.
    // We have:  P1*  P4  P3  P2
    gfx::Point mouse_location = panel1->GetBounds().origin();
    panel1_testing->PressLeftMouseButtonTitlebar(mouse_location);
    mouse_location.Offset(-500, -5);
    panel1_testing->DragTitlebar(mouse_location);
    EXPECT_TRUE(drag_controller->IsDragging());
    EXPECT_EQ(panel1, drag_controller->dragging_panel());

    ASSERT_EQ(4, docked_strip->num_panels());
    panels = PanelManager::GetInstance()->panels();
    EXPECT_EQ(panel2, panels[0]);
    EXPECT_EQ(panel3, panels[1]);
    EXPECT_EQ(panel4, panels[2]);
    EXPECT_EQ(panel1, panels[3]);
    EXPECT_EQ(position1, panel2->GetBounds().origin());
    EXPECT_EQ(position2, panel3->GetBounds().origin());
    EXPECT_EQ(position3, panel4->GetBounds().origin());
    EXPECT_EQ(panel1_new_position, panel1->GetBounds().origin());

    // Closing another panel while dragging in progress will keep the dragging
    // panel intact.
    // We have:  P1*  P4  P3
    CloseWindowAndWait(panel2->browser());
    EXPECT_TRUE(drag_controller->IsDragging());
    EXPECT_EQ(panel1, drag_controller->dragging_panel());

    ASSERT_EQ(3, docked_strip->num_panels());
    panels = PanelManager::GetInstance()->panels();
    EXPECT_EQ(panel3, panels[0]);
    EXPECT_EQ(panel4, panels[1]);
    EXPECT_EQ(panel1, panels[2]);
    EXPECT_EQ(position1, panel3->GetBounds().origin());
    EXPECT_EQ(position2, panel4->GetBounds().origin());
    EXPECT_EQ(panel1_new_position, panel1->GetBounds().origin());

    // Cancel the drag.
    // We have:  P4  P3  P1
    panel1_testing->CancelDragTitlebar();
    EXPECT_FALSE(drag_controller->IsDragging());

    ASSERT_EQ(3, docked_strip->num_panels());
    panels = PanelManager::GetInstance()->panels();
    EXPECT_EQ(panel1, panels[0]);
    EXPECT_EQ(panel3, panels[1]);
    EXPECT_EQ(panel4, panels[2]);
    EXPECT_EQ(position1, panel1->GetBounds().origin());
    EXPECT_EQ(position2, panel3->GetBounds().origin());
    EXPECT_EQ(position3, panel4->GetBounds().origin());
  }

  // Test the scenario: drag a panel, close another panel, end the drag.
  {
    std::vector<Panel*> panels;
    gfx::Point panel1_new_position = position1;
    panel1_new_position.Offset(-500, 0);

    // Start dragging a panel.
    // We have:  P1*  P4  P3
    gfx::Point mouse_location = panel1->GetBounds().origin();
    panel1_testing->PressLeftMouseButtonTitlebar(mouse_location);
    mouse_location.Offset(-500, -5);
    panel1_testing->DragTitlebar(mouse_location);
    EXPECT_TRUE(drag_controller->IsDragging());
    EXPECT_EQ(panel1, drag_controller->dragging_panel());

    ASSERT_EQ(3, docked_strip->num_panels());
    panels = PanelManager::GetInstance()->panels();
    EXPECT_EQ(panel3, panels[0]);
    EXPECT_EQ(panel4, panels[1]);
    EXPECT_EQ(panel1, panels[2]);
    EXPECT_EQ(position1, panel3->GetBounds().origin());
    EXPECT_EQ(position2, panel4->GetBounds().origin());
    EXPECT_EQ(panel1_new_position, panel1->GetBounds().origin());

    // Closing another panel while dragging in progress will keep the dragging
    // panel intact.
    // We have:  P1*  P4
    CloseWindowAndWait(panel3->browser());
    EXPECT_TRUE(drag_controller->IsDragging());
    EXPECT_EQ(panel1, drag_controller->dragging_panel());

    ASSERT_EQ(2, docked_strip->num_panels());
    panels = PanelManager::GetInstance()->panels();
    EXPECT_EQ(panel4, panels[0]);
    EXPECT_EQ(panel1, panels[1]);
    EXPECT_EQ(position1, panel4->GetBounds().origin());
    EXPECT_EQ(panel1_new_position, panel1->GetBounds().origin());

    // Finish the drag.
    // We have:  P1  P4
    panel1_testing->FinishDragTitlebar();
    EXPECT_FALSE(drag_controller->IsDragging());

    ASSERT_EQ(2, docked_strip->num_panels());
    panels = PanelManager::GetInstance()->panels();
    EXPECT_EQ(panel4, panels[0]);
    EXPECT_EQ(panel1, panels[1]);
    EXPECT_EQ(position1, panel4->GetBounds().origin());
    EXPECT_EQ(position2, panel1->GetBounds().origin());
  }

  // Test the scenario: drag a panel and close the dragging panel.
  {
    std::vector<Panel*> panels;
    gfx::Point panel1_new_position = position2;
    panel1_new_position.Offset(-500, 0);

    // Start dragging a panel again.
    // We have:  P1*  P4
    gfx::Point mouse_location = panel1->GetBounds().origin();
    panel1_testing->PressLeftMouseButtonTitlebar(mouse_location);
    mouse_location.Offset(-500, -5);
    panel1_testing->DragTitlebar(mouse_location);
    EXPECT_TRUE(drag_controller->IsDragging());
    EXPECT_EQ(panel1, drag_controller->dragging_panel());
    EXPECT_EQ(panel1_new_position, panel1->GetBounds().origin());

    ASSERT_EQ(2, docked_strip->num_panels());
    panels = PanelManager::GetInstance()->panels();
    EXPECT_EQ(panel4, panels[0]);
    EXPECT_EQ(panel1, panels[1]);
    EXPECT_EQ(position1, panel4->GetBounds().origin());

    // Closing the dragging panel should end the drag.
    // We have:  P4
    CloseWindowAndWait(panel1->browser());
    EXPECT_FALSE(drag_controller->IsDragging());

    ASSERT_EQ(1, docked_strip->num_panels());
    panels = PanelManager::GetInstance()->panels();
    EXPECT_EQ(panel4, panels[0]);
    EXPECT_EQ(position1, panel4->GetBounds().origin());
  }

  panel_manager->CloseAll();
}

IN_PROC_BROWSER_TEST_F(PanelDragBrowserTest, DragOneDetachedPanel) {
  Panel* panel = CreateDetachedPanel("1", gfx::Rect(300, 200, 250, 200));

  // Test that the detached panel can be dragged almost anywhere except getting
  // close to the bottom of the docked area to trigger the attach.
  scoped_ptr<NativePanelTesting> panel_testing(
      NativePanelTesting::Create(panel->native_panel()));
  gfx::Point origin = panel->GetBounds().origin();

  panel_testing->PressLeftMouseButtonTitlebar(origin);
  EXPECT_EQ(origin, panel->GetBounds().origin());

  origin.Offset(-51, -102);
  panel_testing->DragTitlebar(origin);
  EXPECT_EQ(origin, panel->GetBounds().origin());

  origin.Offset(37, 45);
  panel_testing->DragTitlebar(origin);
  EXPECT_EQ(origin, panel->GetBounds().origin());

  panel_testing->FinishDragTitlebar();
  EXPECT_EQ(origin, panel->GetBounds().origin());

  // Test that cancelling the drag will return the panel the the original
  // position.
  gfx::Point original_position = panel->GetBounds().origin();
  origin = original_position;

  panel_testing->PressLeftMouseButtonTitlebar(origin);
  EXPECT_EQ(origin, panel->GetBounds().origin());

  origin.Offset(-51, -102);
  panel_testing->DragTitlebar(origin);
  EXPECT_EQ(origin, panel->GetBounds().origin());

  origin.Offset(37, 45);
  panel_testing->DragTitlebar(origin);
  EXPECT_EQ(origin, panel->GetBounds().origin());

  panel_testing->CancelDragTitlebar();
  WaitForBoundsAnimationFinished(panel);
  EXPECT_EQ(original_position, panel->GetBounds().origin());

  PanelManager::GetInstance()->CloseAll();
}

IN_PROC_BROWSER_TEST_F(PanelDragBrowserTest, CloseDetachedPanelOnDrag) {
  PanelManager* panel_manager = PanelManager::GetInstance();
  PanelDragController* drag_controller = panel_manager->drag_controller();
  DetachedPanelStrip* detached_strip = panel_manager->detached_strip();

  // Create 4 detached panels.
  Panel* panel1 = CreateDetachedPanel("1", gfx::Rect(100, 200, 100, 100));
  Panel* panel2 = CreateDetachedPanel("2", gfx::Rect(200, 210, 110, 110));
  Panel* panel3 = CreateDetachedPanel("3", gfx::Rect(300, 220, 120, 120));
  Panel* panel4 = CreateDetachedPanel("4", gfx::Rect(400, 230, 130, 130));
  ASSERT_EQ(4, detached_strip->num_panels());

  scoped_ptr<NativePanelTesting> panel1_testing(
      NativePanelTesting::Create(panel1->native_panel()));
  gfx::Point panel1_old_position = panel1->GetBounds().origin();
  gfx::Point panel2_position = panel2->GetBounds().origin();
  gfx::Point panel3_position = panel3->GetBounds().origin();
  gfx::Point panel4_position = panel4->GetBounds().origin();

  // Test the scenario: drag a panel, close another panel, cancel the drag.
  {
    gfx::Point panel1_new_position = panel1_old_position;
    panel1_new_position.Offset(-51, -102);

    // Start dragging a panel.
    panel1_testing->PressLeftMouseButtonTitlebar(panel1->GetBounds().origin());
    panel1_testing->DragTitlebar(panel1_new_position);
    EXPECT_TRUE(drag_controller->IsDragging());
    EXPECT_EQ(panel1, drag_controller->dragging_panel());

    ASSERT_EQ(4, detached_strip->num_panels());
    EXPECT_TRUE(detached_strip->HasPanel(panel1));
    EXPECT_TRUE(detached_strip->HasPanel(panel2));
    EXPECT_TRUE(detached_strip->HasPanel(panel3));
    EXPECT_TRUE(detached_strip->HasPanel(panel4));
    EXPECT_EQ(panel1_new_position, panel1->GetBounds().origin());
    EXPECT_EQ(panel2_position, panel2->GetBounds().origin());
    EXPECT_EQ(panel3_position, panel3->GetBounds().origin());
    EXPECT_EQ(panel4_position, panel4->GetBounds().origin());

    // Closing another panel while dragging in progress will keep the dragging
    // panel intact.
    CloseWindowAndWait(panel2->browser());
    EXPECT_TRUE(drag_controller->IsDragging());
    EXPECT_EQ(panel1, drag_controller->dragging_panel());

    ASSERT_EQ(3, detached_strip->num_panels());
    EXPECT_TRUE(detached_strip->HasPanel(panel1));
    EXPECT_TRUE(detached_strip->HasPanel(panel3));
    EXPECT_TRUE(detached_strip->HasPanel(panel4));
    EXPECT_EQ(panel1_new_position, panel1->GetBounds().origin());
    EXPECT_EQ(panel3_position, panel3->GetBounds().origin());
    EXPECT_EQ(panel4_position, panel4->GetBounds().origin());

    // Cancel the drag.
    panel1_testing->CancelDragTitlebar();
    WaitForBoundsAnimationFinished(panel1);
    EXPECT_FALSE(drag_controller->IsDragging());

    ASSERT_EQ(3, detached_strip->num_panels());
    EXPECT_TRUE(detached_strip->HasPanel(panel1));
    EXPECT_TRUE(detached_strip->HasPanel(panel3));
    EXPECT_TRUE(detached_strip->HasPanel(panel4));
    EXPECT_EQ(panel1_old_position, panel1->GetBounds().origin());
    EXPECT_EQ(panel3_position, panel3->GetBounds().origin());
    EXPECT_EQ(panel4_position, panel4->GetBounds().origin());
  }

  // Test the scenario: drag a panel, close another panel, end the drag.
  {
    gfx::Point panel1_new_position = panel1_old_position;
    panel1_new_position.Offset(-51, -102);

    // Start dragging a panel.
    panel1_testing->PressLeftMouseButtonTitlebar(panel1->GetBounds().origin());
    panel1_testing->DragTitlebar(panel1_new_position);
    EXPECT_TRUE(drag_controller->IsDragging());
    EXPECT_EQ(panel1, drag_controller->dragging_panel());

    ASSERT_EQ(3, detached_strip->num_panels());
    EXPECT_TRUE(detached_strip->HasPanel(panel1));
    EXPECT_TRUE(detached_strip->HasPanel(panel3));
    EXPECT_TRUE(detached_strip->HasPanel(panel4));
    EXPECT_EQ(panel1_new_position, panel1->GetBounds().origin());
    EXPECT_EQ(panel3_position, panel3->GetBounds().origin());
    EXPECT_EQ(panel4_position, panel4->GetBounds().origin());

    // Closing another panel while dragging in progress will keep the dragging
    // panel intact.
    CloseWindowAndWait(panel3->browser());
    EXPECT_TRUE(drag_controller->IsDragging());
    EXPECT_EQ(panel1, drag_controller->dragging_panel());

    ASSERT_EQ(2, detached_strip->num_panels());
    EXPECT_TRUE(detached_strip->HasPanel(panel1));
    EXPECT_TRUE(detached_strip->HasPanel(panel4));
    EXPECT_EQ(panel1_new_position, panel1->GetBounds().origin());
    EXPECT_EQ(panel4_position, panel4->GetBounds().origin());

    // Finish the drag.
    panel1_testing->FinishDragTitlebar();
    EXPECT_FALSE(drag_controller->IsDragging());

    ASSERT_EQ(2, detached_strip->num_panels());
    EXPECT_TRUE(detached_strip->HasPanel(panel1));
    EXPECT_TRUE(detached_strip->HasPanel(panel4));
    EXPECT_EQ(panel1_new_position, panel1->GetBounds().origin());
    EXPECT_EQ(panel4_position, panel4->GetBounds().origin());
  }

  // Test the scenario: drag a panel and close the dragging panel.
  {
    gfx::Point panel1_new_position = panel1->GetBounds().origin();
    panel1_new_position.Offset(-51, -102);

    // Start dragging a panel again.
    panel1_testing->PressLeftMouseButtonTitlebar(panel1->GetBounds().origin());
    panel1_testing->DragTitlebar(panel1_new_position);
    EXPECT_TRUE(drag_controller->IsDragging());
    EXPECT_EQ(panel1, drag_controller->dragging_panel());

    ASSERT_EQ(2, detached_strip->num_panels());
    EXPECT_TRUE(detached_strip->HasPanel(panel1));
    EXPECT_TRUE(detached_strip->HasPanel(panel4));
    EXPECT_EQ(panel1_new_position, panel1->GetBounds().origin());
    EXPECT_EQ(panel4_position, panel4->GetBounds().origin());

    // Closing the dragging panel should end the drag.
    CloseWindowAndWait(panel1->browser());
    EXPECT_FALSE(drag_controller->IsDragging());

    ASSERT_EQ(1, detached_strip->num_panels());
    EXPECT_TRUE(detached_strip->HasPanel(panel4));
    EXPECT_EQ(panel4_position, panel4->GetBounds().origin());
  }

  panel_manager->CloseAll();
}

IN_PROC_BROWSER_TEST_F(PanelDragBrowserTest, Detach) {
  PanelManager* panel_manager = PanelManager::GetInstance();
  DockedPanelStrip* docked_strip = panel_manager->docked_strip();
  DetachedPanelStrip* detached_strip = panel_manager->detached_strip();

  // Create one docked panel.
  Panel* panel = CreateDockedPanel("1", gfx::Rect(0, 0, 100, 100));
  ASSERT_EQ(1, docked_strip->num_panels());
  ASSERT_EQ(0, detached_strip->num_panels());

  gfx::Rect panel_old_bounds = panel->GetBounds();

  // Press on title-bar.
  scoped_ptr<NativePanelTesting> panel_testing(
      NativePanelTesting::Create(panel->native_panel()));
  gfx::Point mouse_location(panel->GetBounds().origin());
  panel_testing->PressLeftMouseButtonTitlebar(mouse_location);

  // Drag up the panel in a small offset that does not trigger the detach.
  // Expect that the panel is still docked and only x coordinate of its position
  // is changed.
  gfx::Point drag_delta_to_remain_docked = GetDragDeltaToRemainDocked();
  mouse_location = mouse_location.Add(drag_delta_to_remain_docked);
  panel_testing->DragTitlebar(mouse_location);
  ASSERT_EQ(1, docked_strip->num_panels());
  ASSERT_EQ(0, detached_strip->num_panels());
  EXPECT_EQ(PanelStrip::DOCKED, panel->panel_strip()->type());
  gfx::Rect panel_new_bounds = panel_old_bounds;
  panel_new_bounds.Offset(drag_delta_to_remain_docked.x(), 0);
  EXPECT_EQ(panel_new_bounds, panel->GetBounds());

  // Continue dragging up the panel in big offset that triggers the detach.
  // Expect that the panel is previewed as detached.
  gfx::Point drag_delta_to_detach = GetDragDeltaToDetach();
  mouse_location = mouse_location.Add(drag_delta_to_detach);
  panel_testing->DragTitlebar(mouse_location);
  ASSERT_EQ(0, docked_strip->num_panels());
  ASSERT_EQ(1, detached_strip->num_panels());
  EXPECT_EQ(PanelStrip::DETACHED, panel->panel_strip()->type());
  panel_new_bounds.Offset(
      drag_delta_to_detach.x(),
      drag_delta_to_detach.y() + drag_delta_to_remain_docked.y());
  EXPECT_EQ(panel_new_bounds, panel->GetBounds());

  // Finish the drag.
  // Expect that the panel stays as detached.
  panel_testing->FinishDragTitlebar();
  ASSERT_EQ(0, docked_strip->num_panels());
  ASSERT_EQ(1, detached_strip->num_panels());
  EXPECT_EQ(PanelStrip::DETACHED, panel->panel_strip()->type());
  EXPECT_EQ(panel_new_bounds, panel->GetBounds());

  panel_manager->CloseAll();
}

IN_PROC_BROWSER_TEST_F(PanelDragBrowserTest, DetachAndCancel) {
  PanelManager* panel_manager = PanelManager::GetInstance();
  DockedPanelStrip* docked_strip = panel_manager->docked_strip();
  DetachedPanelStrip* detached_strip = panel_manager->detached_strip();

  // Create one docked panel.
  Panel* panel = CreateDockedPanel("1", gfx::Rect(0, 0, 100, 100));
  ASSERT_EQ(1, docked_strip->num_panels());
  ASSERT_EQ(0, detached_strip->num_panels());

  gfx::Rect panel_old_bounds = panel->GetBounds();

  // Press on title-bar.
  scoped_ptr<NativePanelTesting> panel_testing(
      NativePanelTesting::Create(panel->native_panel()));
  gfx::Point mouse_location(panel->GetBounds().origin());
  panel_testing->PressLeftMouseButtonTitlebar(mouse_location);

  // Drag up the panel in a small offset that does not trigger the detach.
  // Expect that the panel is still docked and only x coordinate of its position
  // is changed.
  gfx::Point drag_delta_to_remain_docked = GetDragDeltaToRemainDocked();
  mouse_location = mouse_location.Add(drag_delta_to_remain_docked);
  panel_testing->DragTitlebar(mouse_location);
  ASSERT_EQ(1, docked_strip->num_panels());
  ASSERT_EQ(0, detached_strip->num_panels());
  EXPECT_EQ(PanelStrip::DOCKED, panel->panel_strip()->type());
  gfx::Rect panel_new_bounds = panel_old_bounds;
  panel_new_bounds.Offset(drag_delta_to_remain_docked.x(), 0);
  EXPECT_EQ(panel_new_bounds, panel->GetBounds());

  // Continue dragging up the panel in big offset that triggers the detach.
  // Expect that the panel is previewed as detached.
  gfx::Point drag_delta_to_detach = GetDragDeltaToDetach();
  mouse_location = mouse_location.Add(drag_delta_to_detach);
  panel_testing->DragTitlebar(mouse_location);
  ASSERT_EQ(0, docked_strip->num_panels());
  ASSERT_EQ(1, detached_strip->num_panels());
  EXPECT_EQ(PanelStrip::DETACHED, panel->panel_strip()->type());
  panel_new_bounds.Offset(
      drag_delta_to_detach.x(),
      drag_delta_to_detach.y() + drag_delta_to_remain_docked.y());
  EXPECT_EQ(panel_new_bounds, panel->GetBounds());

  // Cancel the drag.
  // Expect that the panel is back as docked.
  panel_testing->CancelDragTitlebar();
  ASSERT_EQ(1, docked_strip->num_panels());
  ASSERT_EQ(0, detached_strip->num_panels());
  EXPECT_EQ(PanelStrip::DOCKED, panel->panel_strip()->type());
  EXPECT_EQ(panel_old_bounds, panel->GetBounds());

  panel_manager->CloseAll();
}

IN_PROC_BROWSER_TEST_F(PanelDragBrowserTest, Attach) {
  PanelManager* panel_manager = PanelManager::GetInstance();
  DockedPanelStrip* docked_strip = panel_manager->docked_strip();
  DetachedPanelStrip* detached_strip = panel_manager->detached_strip();

  // Create one detached panel.
  Panel* panel = CreateDetachedPanel("1", gfx::Rect(400, 300, 100, 100));
  ASSERT_EQ(0, docked_strip->num_panels());
  ASSERT_EQ(1, detached_strip->num_panels());
  EXPECT_EQ(PanelStrip::DETACHED, panel->panel_strip()->type());

  gfx::Rect panel_old_bounds = panel->GetBounds();

  // Press on title-bar.
  scoped_ptr<NativePanelTesting> panel_testing(
      NativePanelTesting::Create(panel->native_panel()));
  gfx::Point mouse_location(panel->GetBounds().origin());
  panel_testing->PressLeftMouseButtonTitlebar(mouse_location);

  // Drag down the panel but not close enough to the bottom of work area.
  // Expect that the panel is still detached.
  gfx::Point drag_delta_to_remain_detached =
      GetDragDeltaToRemainDetached(panel);
  mouse_location = mouse_location.Add(drag_delta_to_remain_detached);
  panel_testing->DragTitlebar(mouse_location);
  ASSERT_EQ(0, docked_strip->num_panels());
  ASSERT_EQ(1, detached_strip->num_panels());
  EXPECT_EQ(PanelStrip::DETACHED, panel->panel_strip()->type());
  gfx::Rect panel_new_bounds = panel_old_bounds;
  panel_new_bounds.Offset(drag_delta_to_remain_detached);
  EXPECT_EQ(panel_new_bounds, panel->GetBounds());

  // Continue dragging down the panel to make it close enough to the bottom of
  // work area.
  // Expect that the panel is previewed as docked.
  gfx::Point drag_delta_to_attach = GetDragDeltaToAttach(panel);
  mouse_location = mouse_location.Add(drag_delta_to_attach);
  panel_testing->DragTitlebar(mouse_location);
  ASSERT_EQ(1, docked_strip->num_panels());
  ASSERT_EQ(0, detached_strip->num_panels());
  EXPECT_EQ(PanelStrip::DOCKED, panel->panel_strip()->type());
  panel_new_bounds.Offset(drag_delta_to_attach);
  EXPECT_EQ(panel_new_bounds, panel->GetBounds());

  // Finish the drag.
  // Expect that the panel stays as docked and moves to the final position.
  panel_testing->FinishDragTitlebar();
  ASSERT_EQ(1, docked_strip->num_panels());
  ASSERT_EQ(0, detached_strip->num_panels());
  EXPECT_EQ(PanelStrip::DOCKED, panel->panel_strip()->type());
  panel_new_bounds.set_x(
      docked_strip->StartingRightPosition() - panel_new_bounds.width());
  panel_new_bounds.set_y(
      docked_strip->display_area().bottom() - panel_new_bounds.height());
  EXPECT_EQ(panel_new_bounds, panel->GetBounds());

  panel_manager->CloseAll();
}

IN_PROC_BROWSER_TEST_F(PanelDragBrowserTest, AttachAndCancel) {
  PanelManager* panel_manager = PanelManager::GetInstance();
  DockedPanelStrip* docked_strip = panel_manager->docked_strip();
  DetachedPanelStrip* detached_strip = panel_manager->detached_strip();

  // Create one detached panel.
  Panel* panel = CreateDetachedPanel("1", gfx::Rect(400, 300, 100, 100));
  ASSERT_EQ(0, docked_strip->num_panels());
  ASSERT_EQ(1, detached_strip->num_panels());
  EXPECT_EQ(PanelStrip::DETACHED, panel->panel_strip()->type());

  gfx::Rect panel_old_bounds = panel->GetBounds();

  // Press on title-bar.
  scoped_ptr<NativePanelTesting> panel_testing(
      NativePanelTesting::Create(panel->native_panel()));
  gfx::Point mouse_location(panel->GetBounds().origin());
  panel_testing->PressLeftMouseButtonTitlebar(mouse_location);

  // Drag down the panel but not close enough to the bottom of work area.
  // Expect that the panel is still detached.
  gfx::Point drag_delta_to_remain_detached =
      GetDragDeltaToRemainDetached(panel);
  mouse_location = mouse_location.Add(drag_delta_to_remain_detached);
  panel_testing->DragTitlebar(mouse_location);
  ASSERT_EQ(0, docked_strip->num_panels());
  ASSERT_EQ(1, detached_strip->num_panels());
  EXPECT_EQ(PanelStrip::DETACHED, panel->panel_strip()->type());
  gfx::Rect panel_new_bounds = panel_old_bounds;
  panel_new_bounds.Offset(drag_delta_to_remain_detached);
  EXPECT_EQ(panel_new_bounds, panel->GetBounds());

  // Continue dragging down the panel to make it close enough to the bottom of
  // work area.
  // Expect that the panel is previewed as docked.
  gfx::Point drag_delta_to_attach = GetDragDeltaToAttach(panel);
  mouse_location = mouse_location.Add(drag_delta_to_attach);
  panel_testing->DragTitlebar(mouse_location);
  ASSERT_EQ(1, docked_strip->num_panels());
  ASSERT_EQ(0, detached_strip->num_panels());
  EXPECT_EQ(PanelStrip::DOCKED, panel->panel_strip()->type());
  panel_new_bounds.Offset(drag_delta_to_attach);
  EXPECT_EQ(panel_new_bounds, panel->GetBounds());

  // Cancel the drag.
  // Expect that the panel is back as detached.
  panel_testing->CancelDragTitlebar();
  ASSERT_EQ(0, docked_strip->num_panels());
  ASSERT_EQ(1, detached_strip->num_panels());
  EXPECT_EQ(PanelStrip::DETACHED, panel->panel_strip()->type());
  EXPECT_EQ(panel_old_bounds, panel->GetBounds());

  panel_manager->CloseAll();
}

IN_PROC_BROWSER_TEST_F(PanelDragBrowserTest, DetachAttachAndCancel) {
  PanelManager* panel_manager = PanelManager::GetInstance();
  DockedPanelStrip* docked_strip = panel_manager->docked_strip();
  DetachedPanelStrip* detached_strip = panel_manager->detached_strip();

  // Create one docked panel.
  Panel* panel = CreateDockedPanel("1", gfx::Rect(0, 0, 100, 100));
  ASSERT_EQ(1, docked_strip->num_panels());
  ASSERT_EQ(0, detached_strip->num_panels());

  gfx::Rect panel_old_bounds = panel->GetBounds();

  // Press on title-bar.
  scoped_ptr<NativePanelTesting> panel_testing(
      NativePanelTesting::Create(panel->native_panel()));
  gfx::Point mouse_location(panel->GetBounds().origin());
  panel_testing->PressLeftMouseButtonTitlebar(mouse_location);

  // Drag up the panel to trigger the detach.
  // Expect that the panel is previewed as detached.
  gfx::Point drag_delta_to_detach = GetDragDeltaToDetach();
  mouse_location = mouse_location.Add(drag_delta_to_detach);
  panel_testing->DragTitlebar(mouse_location);
  ASSERT_EQ(0, docked_strip->num_panels());
  ASSERT_EQ(1, detached_strip->num_panels());
  EXPECT_EQ(PanelStrip::DETACHED, panel->panel_strip()->type());
  gfx::Rect panel_new_bounds = panel_old_bounds;
  panel_new_bounds.Offset(drag_delta_to_detach);
  EXPECT_EQ(panel_new_bounds, panel->GetBounds());

  // Continue dragging down the panel to trigger the re-attach.
  gfx::Point drag_delta_to_reattach = GetDragDeltaToAttach(panel);
  mouse_location = mouse_location.Add(drag_delta_to_reattach);
  panel_testing->DragTitlebar(mouse_location);
  ASSERT_EQ(1, docked_strip->num_panels());
  ASSERT_EQ(0, detached_strip->num_panels());
  EXPECT_EQ(PanelStrip::DOCKED, panel->panel_strip()->type());
  panel_new_bounds.Offset(drag_delta_to_reattach);
  EXPECT_EQ(panel_new_bounds, panel->GetBounds());

  // Continue dragging up the panel to trigger the detach again.
  gfx::Point drag_delta_to_detach_again = GetDragDeltaToDetach();
  mouse_location = mouse_location.Add(drag_delta_to_detach_again);
  panel_testing->DragTitlebar(mouse_location);
  ASSERT_EQ(0, docked_strip->num_panels());
  ASSERT_EQ(1, detached_strip->num_panels());
  EXPECT_EQ(PanelStrip::DETACHED, panel->panel_strip()->type());
  panel_new_bounds.Offset(drag_delta_to_detach_again);
  EXPECT_EQ(panel_new_bounds, panel->GetBounds());

  // Cancel the drag.
  // Expect that the panel stays as docked.
  panel_testing->CancelDragTitlebar();
  ASSERT_EQ(1, docked_strip->num_panels());
  ASSERT_EQ(0, detached_strip->num_panels());
  EXPECT_EQ(PanelStrip::DOCKED, panel->panel_strip()->type());
  EXPECT_EQ(panel_old_bounds, panel->GetBounds());

  panel_manager->CloseAll();
}

IN_PROC_BROWSER_TEST_F(PanelDragBrowserTest, DetachWithOverflow) {
  PanelManager* panel_manager = PanelManager::GetInstance();
  DockedPanelStrip* docked_strip = panel_manager->docked_strip();
  DetachedPanelStrip* detached_strip = panel_manager->detached_strip();
  OverflowPanelStrip* overflow_strip = panel_manager->overflow_strip();

  gfx::Point drag_delta_to_detach = GetDragDeltaToDetach();

  // Create some docked and overflow panels.
  //   docked:    P1  P2  P3
  //   overflow:  P4  P5
  Panel* panel1 = CreateDockedPanel("1", gfx::Rect(0, 0, 200, 100));
  Panel* panel2 = CreateDockedPanel("2", gfx::Rect(0, 0, 200, 100));
  Panel* panel3 = CreateDockedPanel("3", gfx::Rect(0, 0, 200, 100));
  Panel* panel4 = CreateOverflowPanel("4", gfx::Rect(0, 0, 200, 100));
  Panel* panel5 = CreateOverflowPanel("5", gfx::Rect(0, 0, 200, 100));
  ASSERT_EQ(0, detached_strip->num_panels());
  ASSERT_EQ(3, docked_strip->num_panels());
  ASSERT_EQ(2, overflow_strip->num_panels());

  gfx::Point docked_position1 = panel1->GetBounds().origin();
  gfx::Point docked_position2 = panel2->GetBounds().origin();
  gfx::Point docked_position3 = panel3->GetBounds().origin();

  // Drag to detach the middle docked panel.
  // Expect to have:
  //   detached:  P2
  //   docked:    P1  P3  P4
  //   overflow:  P5
  DragPanelByDelta(panel2, drag_delta_to_detach);
  ASSERT_EQ(1, detached_strip->num_panels());
  ASSERT_EQ(3, docked_strip->num_panels());
  ASSERT_EQ(1, overflow_strip->num_panels());
  EXPECT_EQ(PanelStrip::DOCKED, panel1->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DETACHED, panel2->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DOCKED, panel3->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DOCKED, panel4->panel_strip()->type());
  EXPECT_EQ(PanelStrip::IN_OVERFLOW, panel5->panel_strip()->type());
  EXPECT_EQ(docked_position1, panel1->GetBounds().origin());
  gfx::Point panel2_new_position = docked_position2.Add(drag_delta_to_detach);
  EXPECT_EQ(panel2_new_position, panel2->GetBounds().origin());
  EXPECT_EQ(docked_position2, panel3->GetBounds().origin());
  EXPECT_EQ(docked_position3, panel4->GetBounds().origin());

  // Drag to detach the left-most docked panel.
  // Expect to have:
  //   detached:  P2  P4
  //   docked:    P1  P3  P5
  DragPanelByDelta(panel4, drag_delta_to_detach);
  ASSERT_EQ(2, detached_strip->num_panels());
  ASSERT_EQ(3, docked_strip->num_panels());
  ASSERT_EQ(0, overflow_strip->num_panels());
  EXPECT_EQ(PanelStrip::DOCKED, panel1->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DETACHED, panel2->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DOCKED, panel3->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DETACHED, panel4->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DOCKED, panel5->panel_strip()->type());
  EXPECT_EQ(docked_position1, panel1->GetBounds().origin());
  EXPECT_EQ(panel2_new_position, panel2->GetBounds().origin());
  EXPECT_EQ(docked_position2, panel3->GetBounds().origin());
  gfx::Point panel4_new_position = docked_position3.Add(drag_delta_to_detach);
  EXPECT_EQ(panel4_new_position, panel4->GetBounds().origin());
  EXPECT_EQ(docked_position3, panel5->GetBounds().origin());

  // Drag to detach the right-most docked panel.
  // Expect to have:
  //   detached:  P1  P2  P4
  //   docked:    P3  P5
  DragPanelByDelta(panel1, drag_delta_to_detach);
  ASSERT_EQ(3, detached_strip->num_panels());
  ASSERT_EQ(2, docked_strip->num_panels());
  ASSERT_EQ(0, overflow_strip->num_panels());
  EXPECT_EQ(PanelStrip::DETACHED, panel1->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DETACHED, panel2->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DOCKED, panel3->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DETACHED, panel4->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DOCKED, panel5->panel_strip()->type());
  gfx::Point panel1_new_position = docked_position1.Add(drag_delta_to_detach);
  EXPECT_EQ(panel1_new_position, panel1->GetBounds().origin());
  EXPECT_EQ(panel2_new_position, panel2->GetBounds().origin());
  EXPECT_EQ(docked_position1, panel3->GetBounds().origin());
  EXPECT_EQ(panel4_new_position, panel4->GetBounds().origin());
  EXPECT_EQ(docked_position2, panel5->GetBounds().origin());

  panel_manager->CloseAll();
}

IN_PROC_BROWSER_TEST_F(PanelDragBrowserTest, AttachWithOverflow) {
  PanelManager* panel_manager = PanelManager::GetInstance();
  DockedPanelStrip* docked_strip = panel_manager->docked_strip();
  DetachedPanelStrip* detached_strip = panel_manager->detached_strip();
  OverflowPanelStrip* overflow_strip = panel_manager->overflow_strip();

  // Create some detached, docked and overflow panels.
  //   detached:  P1  P2  P3
  //   docked:    P4  P5  P6
  //   overflow:  P7
  Panel* panel1 = CreateDetachedPanel("1", gfx::Rect(100, 300, 200, 100));
  Panel* panel2 = CreateDetachedPanel("2", gfx::Rect(200, 300, 200, 100));
  Panel* panel3 = CreateDetachedPanel("3", gfx::Rect(400, 300, 200, 100));
  Panel* panel4 = CreateDockedPanel("4", gfx::Rect(0, 0, 200, 100));
  Panel* panel5 = CreateDockedPanel("5", gfx::Rect(0, 0, 200, 100));
  Panel* panel6 = CreateDockedPanel("6", gfx::Rect(0, 0, 200, 100));
  Panel* panel7 = CreateOverflowPanel("7", gfx::Rect(0, 0, 200, 100));
  ASSERT_EQ(3, detached_strip->num_panels());
  ASSERT_EQ(3, docked_strip->num_panels());
  ASSERT_EQ(1, overflow_strip->num_panels());

  gfx::Point detached_position1 = panel1->GetBounds().origin();
  gfx::Point detached_position2 = panel2->GetBounds().origin();
  gfx::Point detached_position3 = panel3->GetBounds().origin();
  gfx::Point docked_position1 = panel4->GetBounds().origin();
  gfx::Point docked_position2 = panel5->GetBounds().origin();
  gfx::Point docked_position3 = panel6->GetBounds().origin();

  // Drag to attach a detached panel between 2 docked panels.
  // Expect to have:
  //   detached:  P1  P2
  //   docked:    P4  P3  P5
  //   overflow:  P6  P7
  gfx::Point drag_to_location(panel5->GetBounds().x() + 10,
                              panel5->GetBounds().y());
  DragPanelToMouseLocation(panel3, drag_to_location);
  ASSERT_EQ(2, detached_strip->num_panels());
  ASSERT_EQ(3, docked_strip->num_panels());
  ASSERT_EQ(2, overflow_strip->num_panels());
  EXPECT_EQ(PanelStrip::DETACHED, panel1->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DETACHED, panel2->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DOCKED, panel3->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DOCKED, panel4->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DOCKED, panel5->panel_strip()->type());
  EXPECT_EQ(PanelStrip::IN_OVERFLOW, panel6->panel_strip()->type());
  EXPECT_EQ(PanelStrip::IN_OVERFLOW, panel7->panel_strip()->type());
  EXPECT_EQ(detached_position1, panel1->GetBounds().origin());
  EXPECT_EQ(detached_position2, panel2->GetBounds().origin());
  EXPECT_EQ(docked_position2, panel3->GetBounds().origin());
  EXPECT_EQ(docked_position1, panel4->GetBounds().origin());
  EXPECT_EQ(docked_position3, panel5->GetBounds().origin());

  // Drag to attach a detached panel to most-right.
  // Expect to have:
  //   detached:  P1
  //   docked:    P2  P4  P3
  //   overflow:  P5  P6  P7
  gfx::Point drag_to_location2(panel4->GetBounds().right() + 10,
                               panel4->GetBounds().y());
  DragPanelToMouseLocation(panel2, drag_to_location2);
  ASSERT_EQ(1, detached_strip->num_panels());
  ASSERT_EQ(3, docked_strip->num_panels());
  ASSERT_EQ(3, overflow_strip->num_panels());
  EXPECT_EQ(PanelStrip::DETACHED, panel1->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DOCKED, panel2->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DOCKED, panel3->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DOCKED, panel4->panel_strip()->type());
  EXPECT_EQ(PanelStrip::IN_OVERFLOW, panel5->panel_strip()->type());
  EXPECT_EQ(PanelStrip::IN_OVERFLOW, panel6->panel_strip()->type());
  EXPECT_EQ(PanelStrip::IN_OVERFLOW, panel7->panel_strip()->type());
  EXPECT_EQ(detached_position1, panel1->GetBounds().origin());
  EXPECT_EQ(docked_position1, panel2->GetBounds().origin());
  EXPECT_EQ(docked_position3, panel3->GetBounds().origin());
  EXPECT_EQ(docked_position2, panel4->GetBounds().origin());

  // Drag to attach a detached panel to most-left.
  // Expect to have:
  //   docked:    P2  P4  P1
  //   overflow:  P3  P5  P6  P7
  gfx::Point drag_to_location3(panel3->GetBounds().x() - 10,
                               panel3->GetBounds().y());
  DragPanelToMouseLocation(panel1, drag_to_location3);
  ASSERT_EQ(0, detached_strip->num_panels());
  ASSERT_EQ(3, docked_strip->num_panels());
  ASSERT_EQ(4, overflow_strip->num_panels());
  EXPECT_EQ(PanelStrip::DOCKED, panel1->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DOCKED, panel2->panel_strip()->type());
  EXPECT_EQ(PanelStrip::IN_OVERFLOW, panel3->panel_strip()->type());
  EXPECT_EQ(PanelStrip::DOCKED, panel4->panel_strip()->type());
  EXPECT_EQ(PanelStrip::IN_OVERFLOW, panel5->panel_strip()->type());
  EXPECT_EQ(PanelStrip::IN_OVERFLOW, panel6->panel_strip()->type());
  EXPECT_EQ(PanelStrip::IN_OVERFLOW, panel7->panel_strip()->type());
  EXPECT_EQ(docked_position3, panel1->GetBounds().origin());
  EXPECT_EQ(docked_position1, panel2->GetBounds().origin());
  EXPECT_EQ(docked_position2, panel4->GetBounds().origin());

  panel_manager->CloseAll();
}
