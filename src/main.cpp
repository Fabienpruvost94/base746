#include "lvgl.h"
#include <Arduino.h>
#include <math.h>
#include "lvglDrivers.h"

LV_IMAGE_DECLARE(fleche);

#define AS5047P_CS 9
#define AS5047P_MOSI 7
#define AS5047P_MISO 8
#define AS5047P_SCK 6
#define AS5047P_NOP 0x0000
#define AS5047P_ANGLECOM 0x3FFE

static const int32_t OFFSET_ANGLE = 3600;

enum EtatJeu
{
  JeuEnAttente,
  JeuAttenteMouvement,
  JeuEnRotation,
  JeuEvaluation
};

static lv_obj_t *label_angle;
static lv_obj_t *roulette;
static lv_obj_t *bouton_rouge;
static lv_obj_t *bouton_noir;
static lv_obj_t *aiguille;
static lv_display_t *ecran;
static lv_obj_t *cont_boutons;
static lv_obj_t *bouton_depart;
static lv_obj_t *cont_cercle;
static lv_obj_t *label_statut;
static lv_obj_t *label_solde;
static lv_obj_t *label_solde_projete; 

static int couleur_selectionnee = 0;
static EtatJeu etat_actuel = JeuEnAttente;
static float angle_initial = 0.0f;
static float dernier_angle = 0.0f;
static int compteur_stabilite = 0;
static bool depart_declenche = false;

static int solde = 200;
static int mise_courante = 5;

static lv_obj_t *token_5;
static lv_obj_t *token_10;
static lv_obj_t *token_20;

static int numeros_selectionnes[5] = {0, 0, 0, 0, 0};
static int nb_numeros_selectionnes = 0;

static lv_point_precise_t points_segments[36][2];
static lv_obj_t *segments[36];

uint16_t transfert_SPI_logiciel(uint16_t valeur)
{
  uint16_t sortie = 0;
  for (int i = 15; i >= 0; i--)
  {
    digitalWrite(AS5047P_MOSI, (valeur & (1 << i)) ? HIGH : LOW);
    delayMicroseconds(5);
    digitalWrite(AS5047P_SCK, HIGH);
    delayMicroseconds(5);
    digitalWrite(AS5047P_SCK, LOW);
    delayMicroseconds(5);
    if (digitalRead(AS5047P_MISO))
      sortie |= (1 << i);
  }
  return sortie;
}

uint16_t lire_AS5047P()
{
  uint16_t commande = 0x4000 | AS5047P_ANGLECOM;
  digitalWrite(AS5047P_CS, LOW);
  delayMicroseconds(5);
  transfert_SPI_logiciel(commande);
  digitalWrite(AS5047P_CS, HIGH);
  delayMicroseconds(10);
  digitalWrite(AS5047P_CS, LOW);
  delayMicroseconds(5);
  uint16_t reponse = transfert_SPI_logiciel(AS5047P_NOP);
  digitalWrite(AS5047P_CS, HIGH);
  return (reponse & 0x3FF0);
}



int calculer_cout_total()
{
  int nb_mises = nb_numeros_selectionnes + (couleur_selectionnee > 0 ? 1 : 0);
  return nb_mises * mise_courante;
}

void mettre_a_jour_solde_projete()
{
  int cout = calculer_cout_total();
  int estimation = solde - cout;
  
  lv_label_set_text_fmt(label_solde_projete, "Apres mise: %d$", estimation);
  
  if (estimation < 0) {
    lv_obj_set_style_text_color(label_solde_projete, lv_color_hex(0xFF0000), 0); 
  } else {
    lv_obj_set_style_text_color(label_solde_projete, lv_color_hex(0xAAAAAA), 0); 
  }
}

void reinitialiser_mises()
{
  
  for (int i = 0; i < 5; i++) numeros_selectionnes[i] = 0;
  nb_numeros_selectionnes = 0;
  couleur_selectionnee = 0;

  
  lv_obj_clear_state(bouton_rouge, LV_STATE_CHECKED);
  lv_obj_clear_state(bouton_noir, LV_STATE_CHECKED);

  
  uint32_t child_cnt = lv_obj_get_child_count(cont_boutons);
  for (uint32_t i = 0; i < child_cnt; i++) {
    lv_obj_t * child = lv_obj_get_child(cont_boutons, i);
    lv_obj_clear_state(child, LV_STATE_CHECKED);
  }

  mettre_a_jour_solde_projete();
}



static void mettre_a_jour_tokens()
{
  lv_color_t couleur_active = lv_color_hex(0xFFD700);
  lv_color_t couleur_inactive = lv_color_hex(0x555555);

  lv_obj_set_style_bg_color(token_5, mise_courante == 5 ? couleur_active : couleur_inactive, LV_PART_MAIN);
  lv_obj_set_style_bg_color(token_10, mise_courante == 10 ? couleur_active : couleur_inactive, LV_PART_MAIN);
  lv_obj_set_style_bg_color(token_20, mise_courante == 20 ? couleur_active : couleur_inactive, LV_PART_MAIN);
}

static void cb_token_mise(lv_event_t *e)
{
  lv_obj_t *btn = (lv_obj_t *)lv_event_get_current_target(e);
  if (!btn) return;

  mise_courante = (int)(uintptr_t)lv_obj_get_user_data(btn);
  mettre_a_jour_tokens();
  mettre_a_jour_solde_projete(); // MAJ du stock
}

static void cb_bouton_couleur(lv_event_t *e)
{
  lv_obj_t *btn = (lv_obj_t *)lv_event_get_current_target(e);
  if (!btn) return;

  bool coche = lv_obj_has_state(btn, LV_STATE_CHECKED);

  if (btn == bouton_rouge)
  {
    if (coche)
    {
      couleur_selectionnee = 1;
      lv_obj_clear_state(bouton_noir, LV_STATE_CHECKED);
    }
    else
    {
      couleur_selectionnee = 0;
    }
  }
  else if (btn == bouton_noir)
  {
    if (coche)
    {
      couleur_selectionnee = 2;
      lv_obj_clear_state(bouton_rouge, LV_STATE_CHECKED);
    }
    else
    {
      couleur_selectionnee = 0;
    }
  }
  mettre_a_jour_solde_projete(); 
}

static void cb_bouton_numero(lv_event_t *e)
{
  lv_obj_t *btn = (lv_obj_t *)lv_event_get_current_target(e);
  if (!btn) return;

  int num_courant = (int)(uintptr_t)lv_obj_get_user_data(btn);
  bool est_coche = lv_obj_has_state(btn, LV_STATE_CHECKED);

  if (est_coche)
  {
    if (nb_numeros_selectionnes >= 5)
    {
      lv_obj_clear_state(btn, LV_STATE_CHECKED);
      lv_label_set_text(label_statut, "Maximum 5 numeros !");
      lv_obj_set_style_text_color(label_statut, lv_color_hex(0xFF0000), 0);
      return;
    }

    for (int i = 0; i < 5; i++)
    {
      if (numeros_selectionnes[i] == 0)
      {
        numeros_selectionnes[i] = num_courant;
        nb_numeros_selectionnes++;
        break;
      }
    }
  }
  else
  {
    for (int i = 0; i < 5; i++)
    {
      if (numeros_selectionnes[i] == num_courant)
      {
        numeros_selectionnes[i] = 0;
        nb_numeros_selectionnes--;
        break;
      }
    }
  }
  mettre_a_jour_solde_projete(); 
}

static void cb_bouton_depart(lv_event_t *e)
{
  lv_obj_t *btn = (lv_obj_t *)lv_event_get_current_target(e);
  if (!btn) return;

  depart_declenche = lv_obj_has_state(btn, LV_STATE_CHECKED);
}

void initialiser_interface_LVGL()
{
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x064000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, LV_PART_MAIN);

  label_statut = lv_label_create(lv_screen_active());
  lv_label_set_text(label_statut, "Pret");
  lv_obj_set_style_text_color(label_statut, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(label_statut, LV_ALIGN_TOP_MID, -20, 15);

  label_solde = lv_label_create(lv_screen_active());
  lv_label_set_text_fmt(label_solde, "Solde: %d$", solde);
  lv_obj_set_style_text_color(label_solde, lv_color_hex(0xFFFF00), 0);
  lv_obj_align(label_solde, LV_ALIGN_BOTTOM_RIGHT, -20, -15);

  
  label_solde_projete = lv_label_create(lv_screen_active());
  lv_label_set_text_fmt(label_solde_projete, "Apres mise: %d$", solde);
  lv_obj_set_style_text_color(label_solde_projete, lv_color_hex(0xAAAAAA), 0);
  lv_obj_align(label_solde_projete, LV_ALIGN_BOTTOM_RIGHT, -10, -40);

  roulette = lv_arc_create(lv_screen_active());
  lv_arc_set_bg_angles(roulette, 0, 360);
  lv_arc_set_angles(roulette, 0, 360);
  lv_arc_set_range(roulette, 0, 360);
  lv_arc_set_value(roulette, 0);
  lv_obj_set_size(roulette, 180, 180);
  lv_obj_align(roulette, LV_ALIGN_CENTER, -130, 0);
  lv_obj_remove_style(roulette, NULL, LV_PART_KNOB);
  lv_obj_remove_style(roulette, NULL, LV_PART_INDICATOR);

  cont_cercle = lv_obj_create(lv_screen_active());
  lv_obj_set_size(cont_cercle, 190, 190);
  lv_obj_align(cont_cercle, LV_ALIGN_CENTER, -130, 0);
  lv_obj_set_style_radius(cont_cercle, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(cont_cercle, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont_cercle, 0, 0);
  lv_obj_set_style_pad_all(cont_cercle, 0, 0);
  lv_obj_clear_flag(cont_cercle, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  float centre_x = 95.0f, centre_y = 95.0f;

  for (int i = 0; i < 36; i++)
  {
    int numero = i + 1;
    float angle_milieu_deg = i * 10.0f + 5.0f;
    float angle_milieu_rad = angle_milieu_deg * (M_PI / 180.0f);

    float rayon_x = centre_x + 83.0f * cosf(angle_milieu_rad);
    float rayon_y = centre_y + 83.0f * sinf(angle_milieu_rad);

    lv_obj_t *case_rectangle = lv_obj_create(cont_cercle);
    lv_obj_set_size(case_rectangle, 14, 14);
    lv_obj_set_style_border_width(case_rectangle, 0, 0);
    lv_obj_set_style_pad_all(case_rectangle, 0, 0);
    lv_obj_set_style_radius(case_rectangle, 0, 0);
    lv_obj_set_pos(case_rectangle, (lv_coord_t)(rayon_x - 7), (lv_coord_t)(rayon_y - 7));

    lv_obj_set_style_transform_pivot_x(case_rectangle, 7, 0);
    lv_obj_set_style_transform_pivot_y(case_rectangle, 7, 0);
    int32_t angle_lvgl = (int32_t)((angle_milieu_deg + 90.0f) * 10.0f);
    lv_obj_set_style_transform_rotation(case_rectangle, angle_lvgl, 0);

    if (numero % 2 == 0)
    {
      lv_obj_set_style_bg_color(case_rectangle, lv_color_hex(0xFF0000), 0);
    }
    else
    {
      lv_obj_set_style_bg_color(case_rectangle, lv_color_hex(0x111111), 0);
    }

    lv_obj_t *lbl = lv_label_create(case_rectangle);
    lv_label_set_text_fmt(lbl, "%d", numero);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl);
  }

  aiguille = lv_image_create(lv_screen_active());
  lv_image_set_src(aiguille, &fleche);
  lv_image_set_pivot(aiguille, 30, 20);
  lv_obj_align(aiguille, LV_ALIGN_CENTER, -130, 0);
  lv_image_set_scale(aiguille, 512);
  lv_image_set_rotation(aiguille, OFFSET_ANGLE);

  cont_boutons = lv_obj_create(lv_screen_active());
  lv_obj_set_size(cont_boutons, 180, 150);
  lv_obj_align(cont_boutons, LV_ALIGN_CENTER, 150, 0);
  lv_obj_set_style_bg_color(cont_boutons, lv_color_hex(0x064000), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_flex_flow(cont_boutons, LV_FLEX_FLOW_ROW_WRAP);

  for (uint32_t i = 1; i < 37; i++)
  {
    lv_obj_t *btn = lv_button_create(cont_boutons);
    lv_obj_set_size(btn, 40, 50);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_user_data(btn, (void *)(uintptr_t)i);

    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2196F3), LV_PART_MAIN | LV_STATE_DEFAULT);

    if (i % 2 == 0)
    {
      lv_obj_set_style_bg_color(btn, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_CHECKED);
    }
    else
    {
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_CHECKED);
    }

    lv_obj_remove_style(btn, NULL, LV_STATE_PRESSED);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text_fmt(label, "%" LV_PRIu32, i);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, cb_bouton_numero, LV_EVENT_CLICKED, NULL);
  }

  bouton_rouge = lv_button_create(lv_screen_active());
  lv_obj_set_size(bouton_rouge, 50, 60);
  lv_obj_add_flag(bouton_rouge, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_align(bouton_rouge, LV_ALIGN_CENTER, 30, 40);
  lv_obj_set_style_bg_color(bouton_rouge, lv_color_hex(0x006400), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(bouton_rouge, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_CHECKED);
  lv_obj_remove_style(bouton_rouge, NULL, LV_STATE_PRESSED);
  lv_obj_add_event_cb(bouton_rouge, cb_bouton_couleur, LV_EVENT_CLICKED, NULL);
  lv_obj_t *labelrouge = lv_label_create(bouton_rouge);
  lv_label_set_text(labelrouge, "Rouge");
  lv_obj_center(labelrouge);

  bouton_noir = lv_button_create(lv_screen_active());
  lv_obj_set_size(bouton_noir, 50, 60);
  lv_obj_add_flag(bouton_noir, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_align(bouton_noir, LV_ALIGN_CENTER, 30, -40);
  lv_obj_set_style_bg_color(bouton_noir, lv_color_hex(0x006400), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(bouton_noir, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_CHECKED);
  lv_obj_remove_style(bouton_noir, NULL, LV_STATE_PRESSED);
  lv_obj_add_event_cb(bouton_noir, cb_bouton_couleur, LV_EVENT_CLICKED, NULL);
  lv_obj_t *labelnoir = lv_label_create(bouton_noir);
  lv_label_set_text(labelnoir, "Noir");
  lv_obj_center(labelnoir);

  bouton_depart = lv_button_create(lv_screen_active());
  lv_obj_set_size(bouton_depart, 150, 50);
  lv_obj_add_flag(bouton_depart, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_align(bouton_depart, LV_ALIGN_CENTER, 150, -105);
  lv_obj_set_style_bg_color(bouton_depart, lv_color_hex(0x006400), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(bouton_depart, lv_color_hex(0x000088), LV_PART_MAIN | LV_STATE_CHECKED);
  lv_obj_remove_style(bouton_depart, NULL, LV_STATE_PRESSED);
  lv_obj_add_event_cb(bouton_depart, cb_bouton_depart, LV_EVENT_CLICKED, NULL);
  lv_obj_t *labelStart = lv_label_create(bouton_depart);
  lv_label_set_text(labelStart, "START");
  lv_obj_center(labelStart);

  token_5 = lv_button_create(lv_screen_active());
  lv_obj_set_size(token_5, 35, 35);
  lv_obj_set_style_radius(token_5, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(token_5, lv_color_hex(0xFFD700), LV_PART_MAIN);
  lv_obj_set_style_border_color(token_5, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_border_width(token_5, 2, LV_PART_MAIN);
  lv_obj_remove_style(token_5, NULL, LV_STATE_PRESSED);
  lv_obj_set_user_data(token_5, (void *)(uintptr_t)5);
  lv_obj_add_event_cb(token_5, cb_token_mise, LV_EVENT_CLICKED, NULL);
  lv_obj_align(token_5, LV_ALIGN_BOTTOM_LEFT, 300, -10);
  lv_obj_t *lbl_t5 = lv_label_create(token_5);
  lv_label_set_text(lbl_t5, "5");
  lv_obj_set_style_text_color(lbl_t5, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(lbl_t5);

  token_10 = lv_button_create(lv_screen_active());
  lv_obj_set_size(token_10, 35, 35);
  lv_obj_set_style_radius(token_10, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(token_10, lv_color_hex(0x555555), LV_PART_MAIN);
  lv_obj_set_style_border_color(token_10, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_border_width(token_10, 2, LV_PART_MAIN);
  lv_obj_remove_style(token_10, NULL, LV_STATE_PRESSED);
  lv_obj_set_user_data(token_10, (void *)(uintptr_t)10);
  lv_obj_add_event_cb(token_10, cb_token_mise, LV_EVENT_CLICKED, NULL);
  lv_obj_align(token_10, LV_ALIGN_BOTTOM_LEFT, 255, -10);
  lv_obj_t *lbl_t10 = lv_label_create(token_10);
  lv_label_set_text(lbl_t10, "10");
  lv_obj_set_style_text_color(lbl_t10, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(lbl_t10);

  token_20 = lv_button_create(lv_screen_active());
  lv_obj_set_size(token_20, 35, 35);
  lv_obj_set_style_radius(token_20, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(token_20, lv_color_hex(0x555555), LV_PART_MAIN);
  lv_obj_set_style_border_color(token_20, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_border_width(token_20, 2, LV_PART_MAIN);
  lv_obj_remove_style(token_20, NULL, LV_STATE_PRESSED);
  lv_obj_set_user_data(token_20, (void *)(uintptr_t)20);
  lv_obj_add_event_cb(token_20, cb_token_mise, LV_EVENT_CLICKED, NULL);
  lv_obj_align(token_20, LV_ALIGN_BOTTOM_LEFT, 210, -10);
  lv_obj_t *lbl_t20 = lv_label_create(token_20);
  lv_label_set_text(lbl_t20, "20");
  lv_obj_set_style_text_color(lbl_t20, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(lbl_t20);
}

#ifdef ARDUINO

void mySetup()
{
  Serial.begin(115200);
  delay(2500);
  pinMode(AS5047P_CS, OUTPUT);
  pinMode(AS5047P_SCK, OUTPUT);
  pinMode(AS5047P_MOSI, OUTPUT);
  pinMode(AS5047P_MISO, INPUT);
  digitalWrite(AS5047P_CS, HIGH);
  digitalWrite(AS5047P_SCK, LOW);
  initialiser_interface_LVGL();
}

void loop()
{
}

void myTask(void *pvParameters)
{
  TickType_t temps_dernier_reveil;
  temps_dernier_reveil = xTaskGetTickCount();

  while (1)
  {
    uint16_t valeur_angle = lire_AS5047P();
    float angle_degres = (valeur_angle * 360.0f) / 16384.0f;

    int angle_entier = (int)angle_degres;

    if (roulette)
    {
      lvglLock();
      lv_arc_set_value(roulette, angle_entier);
      lvglUnlock();
    }

    if (aiguille)
    {
      lvglLock();
      int32_t angle_lvgl = (int32_t)(angle_degres * 10.0f) + OFFSET_ANGLE;
      angle_lvgl = ((angle_lvgl % 3600) + 3600) % 3600;
      lv_image_set_rotation(aiguille, angle_lvgl);
      lvglUnlock();
    }

    switch (etat_actuel)
    {
    case JeuEnAttente:
      if (depart_declenche)
      {
        int cout_total = calculer_cout_total();

        if (cout_total == 0)
        {
          lvglLock();
          lv_label_set_text(label_statut, "Misez d'abord !");
          lv_obj_set_style_text_color(label_statut, lv_color_hex(0xFF0000), 0);
          lv_obj_clear_state(bouton_depart, LV_STATE_CHECKED);
          lvglUnlock();
          depart_declenche = false;
          break;
        }

        if (solde < cout_total)
        {
          lvglLock();
          lv_label_set_text(label_statut, "Solde insuffisant !");
          lv_obj_set_style_text_color(label_statut, lv_color_hex(0xFF0000), 0);
          lv_obj_clear_state(bouton_depart, LV_STATE_CHECKED);
          lvglUnlock();
          depart_declenche = false;
          break;
        }

        solde -= cout_total;
        
        lvglLock();
        lv_label_set_text_fmt(label_solde, "Solde: %d$", solde);
        lv_label_set_text_fmt(label_solde_projete, "Apres mise: %d$", solde);
        lv_obj_set_style_text_color(label_solde_projete, lv_color_hex(0xAAAAAA), 0);
        
        angle_initial = angle_degres;
        dernier_angle = angle_degres;
        compteur_stabilite = 0;
        etat_actuel = JeuAttenteMouvement;
        
        lv_label_set_text(label_statut, "Lancez la roulette !");
        lv_obj_set_style_text_color(label_statut, lv_color_hex(0xFFFFFF), 0);
        lvglUnlock();
      }
      break;

    case JeuAttenteMouvement:
      if (!depart_declenche)
      {
        etat_actuel = JeuEnAttente;
        lvglLock();
        lv_label_set_text(label_statut, "Pret");
        lvglUnlock();
        break;
      }
      {
        float difference = fabs(angle_degres - angle_initial);
        if (difference > 180.0f)
          difference = 360.0f - difference;
        if (difference >= 10.0f)
        {
          etat_actuel = JeuEnRotation;
          dernier_angle = angle_degres;
          compteur_stabilite = 0;
          lvglLock();
          lv_label_set_text(label_statut, "En mouvement...");
          lvglUnlock();
        }
      }
      break;

    case JeuEnRotation:
    {
      float diff_dernier = fabs(angle_degres - dernier_angle);
      if (diff_dernier > 180.0f)
        diff_dernier = 360.0f - diff_dernier;

      if (diff_dernier < 0.5f)
      {
        compteur_stabilite++;
      }
      else
      {
        compteur_stabilite = 0;
      }

      dernier_angle = angle_degres;

      if (compteur_stabilite >= 10)
      {
        etat_actuel = JeuEvaluation;
      }
    }
    break;

    case JeuEvaluation:
    {
      int secteur = (int)(angle_degres / 10.0f);
      if (secteur < 0) secteur = 0;
      if (secteur > 35) secteur = 35;
      
      int numero_gagnant = secteur + 1;
      int couleur_gagnante = (numero_gagnant % 2 == 0) ? 1 : 2;

      int gains = 0;

      if (nb_numeros_selectionnes > 0)
      {
        for (int i = 0; i < 5; i++)
        {
          if (numeros_selectionnes[i] == numero_gagnant)
          {
            gains += (mise_courante * 35);
            break;
          }
        }
      }

      if (couleur_selectionnee > 0 && couleur_selectionnee == couleur_gagnante)
      {
        gains += (mise_courante * 2);
      }

      lvglLock();
      if (gains > 0)
      {
        solde += gains;
        lv_label_set_text_fmt(label_statut, "GAGNE %d$! (Num %d)", gains, numero_gagnant);
        lv_obj_set_style_text_color(label_statut, lv_color_hex(0x00FF00), 0);
      }
      else
      {
        lv_label_set_text_fmt(label_statut, "PERDU ! (Num %d)", numero_gagnant);
        lv_obj_set_style_text_color(label_statut, lv_color_hex(0xFF0000), 0);
      }

      lv_label_set_text_fmt(label_solde, "Solde: %d$", solde);
      lv_obj_clear_state(bouton_depart, LV_STATE_CHECKED);
      
    
      reinitialiser_mises();
      
      lvglUnlock();

      depart_declenche = false;
      etat_actuel = JeuEnAttente;
    }
    break;
    }

    vTaskDelayUntil(&temps_dernier_reveil, pdMS_TO_TICKS(75));
  }
}

#else

int main(void)
{
  return 0;
}

#endif