/* PB10A OLED bring-up menu. No power/PD services or localization. */
#include "ui_conf.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "images/image.h"

static ui_page_t s_root, s_display;
static ui_item_t s_demo, s_display_item, s_back, s_about;

void Create_Parameter(ui_t *ui)
{
    Create_Disp_Parameters(ui);
}

void Create_Text(ui_t *ui)
{
    static ui_text_t demo_text, about_text;
    static ui_element_t demo_element, about_element;
    (void)ui;

    demo_text.ptr = "PB10A OLED READY\nSSD1306 128x64\nSPI1 TX DMA\nCPEN + charge pump";
    demo_text.font = UI_FONT;
    demo_text.fontHight = UI_FONT_HIGHT;
    demo_text.fontWidth = UI_FONT_WIDTH;
    demo_element.text = &demo_text;
    Create_element(&s_demo, &demo_element);

    about_text.ptr = "PB10A Bring-up\nSTM32G070KBTx\nMiaoUI + u8g2\ncoroOS scheduler";
    about_text.font = UI_FONT;
    about_text.fontHight = UI_FONT_HIGHT;
    about_text.fontWidth = UI_FONT_WIDTH;
    about_element.text = &about_text;
    Create_element(&s_about, &about_element);
}

void Create_MenuTree(ui_t *ui)
{
    (void)ui;
    AddPage("[PB10A]", &s_root, UI_PAGE_ICON, 0);
    AddItem("-OLED Test", UI_ITEM_WORD, img_burn_in, &s_demo, &s_root, 0, 0);
    AddItem("-Display", UI_ITEM_PARENTS, img_configuration, &s_display_item, &s_root, &s_display, 0);
    AddPage("[Display]", &s_display, UI_PAGE_TEXT, &s_root);
    AddItem("[Return]", UI_ITEM_RETURN, 0, &s_back, &s_display, &s_root, 0);
    Add_Disp_Items(&s_display);
    AddItem("-About", UI_ITEM_WORD, img_user_account, &s_about, &s_root, 0, 0);
}

void MiaoUi_Setup(ui_t *ui)
{
    Create_UI(ui, &s_demo);
}
