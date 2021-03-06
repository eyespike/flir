#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <cairo.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include<string.h>
#include<pthread.h>
#include<stdlib.h>
#include<unistd.h>
//#include "MagickCore.h"
#include "wand/magick-wand.h"

#include "monitor.h"
#include "tdio.h"



// --- UI Global Variables
GMainContext *mainc;
GtkApplication *app_ui;
GtkWindow *main_window;
GtkLabel *statusLabel;
GtkButton *monitorButton;
GtkImage *captureImage;
GtkDrawingArea *videoArea;


GtkImage *demoStart;
GtkImage *demoPossibleFlooding;
GtkImage *demoFloodingConfirmed;
GtkImage *demoWaterShutOff;
GtkImage *demoNotificationSent;


unsigned char *videoFrameBlock;

 typedef struct {
	GtkEntry *waterDiff;
	GtkEntry *bodyDiff;
	GtkEntry *fireDiff;
	GtkEntry *minWaterPC;
	GtkEntry *minBodyPC;
	GtkEntry *minFirePC;
	GtkEntry *floodDetectDelay;
	GtkEntry *floodConfirmedDelay;
	GtkEntry *noticeSentDelay;
} VariableEntries;

static VariableEntries vEntries;

pthread_t tid[2];
bool _active = false;


// --- Forward Declarations
//static void toggle_status (GtkWidget *widget, gpointer *data);



/*********************************** Video **********************************/
// <editor-fold>
gboolean video_area_expose (GtkWidget *da, gpointer data)
{
	GdkPixbuf *pix;
		
	pix = gdk_pixbuf_new_from_data (
			(guchar*)videoFrameBlock,
			GDK_COLORSPACE_RGB,
			FALSE,
			8, 757, 568, (757*3),
			//8, 80, 60, (80*3),
			NULL,
			NULL);
	
    cairo_t *cr;
    cr = gdk_cairo_create (gtk_widget_get_window(da));
	//cairo_scale(cr, 9.5, 9.5);
    gdk_cairo_set_source_pixbuf(cr, pix, 0, 10);
	cairo_paint(cr);
    cairo_destroy (cr);
	
	return FALSE;
}


static gboolean init_video_area (GtkWidget *da, gpointer data)
{
	GdkPixbuf *pix;
	GError *err = NULL;
	pix = gdk_pixbuf_new_from_file ("demo_video_placeholder.png", &err);
	
    cairo_t *cr;
    cr = gdk_cairo_create (gtk_widget_get_window(da));
    gdk_cairo_set_source_pixbuf(cr, pix, 0, 10);
    cairo_paint(cr);
    cairo_destroy (cr);
	
	return FALSE;
}

static void start_video_feed()
{

	_active = true;
	int err;
	err = pthread_create(&(tid[0]), NULL, &iterate_lepton, NULL);
	if(err != 0)
		printf("Threading error: [%s]\n", strerror(err));
	else
		printf("Video feed started\n");
}

static void stop_video_feed()
{
	_active = false;
}

// </editor-fold>


/*********************************** Capture **********************************/
// <editor-fold>

void set_capture_image_from_current_array(GtkImage *image)
{
	char* imageName = save_pgm_file();
	
	// Scale the image up and display it (using ImageMagick commands)
	char largeImageName[34];
	snprintf(largeImageName, sizeof largeImageName, "l_%s", imageName);
	
	char scaleCommand[100];
	snprintf(scaleCommand, sizeof scaleCommand, "convert %s -resize 600%% %s", imageName, largeImageName);
	system(scaleCommand);
	
	gtk_image_set_from_file (image, largeImageName);	
	
	int ret;
	ret = remove(imageName);
	ret = remove(largeImageName);
}


static void capture_image (GtkWidget *widget, gpointer *data)
{
	int fd = connect_to_lepton();
	while(transfer(fd)!=59){}
	close(fd);
	set_capture_image_from_current_array(captureImage);
}

// </editor-fold>


/*********************************** Calibration & Settings **********************************/
// <editor-fold>

//static void save_settings (GtkWidget *widget, VariableEntries *data)
static void save_settings (GtkWidget *widget, gpointer data)
{

	//VariableEntries *d = (VariableEntries*)data;
	
	water_tc_differential = (int)strtol(gtk_entry_get_text (vEntries.waterDiff), (char **) NULL, 10);
	body_tc_differential = (int)strtol(gtk_entry_get_text (vEntries.bodyDiff), (char **) NULL, 10);
	fire_tc_differential = (int)strtol(gtk_entry_get_text (vEntries.fireDiff), (char **) NULL, 10);
	water_min_detected_pc = (int)strtol(gtk_entry_get_text (vEntries.minWaterPC), (char **) NULL, 10);
	body_min_detected_pc = (int)strtol(gtk_entry_get_text (vEntries.minBodyPC), (char **) NULL, 10);
	fire_min_detected_pc = (int)strtol(gtk_entry_get_text (vEntries.minFirePC), (char **) NULL, 10);
	WATER_DETECTED_SECS = (int)strtol(gtk_entry_get_text (vEntries.floodDetectDelay), (char **) NULL, 10);
	WATER_DETECTED_CONFIRMED_SECS_TO_SHUTDOWN = (int)strtol(gtk_entry_get_text (vEntries.floodConfirmedDelay), (char **) NULL, 10);
	NOTIFCATION_SENT_DELAY_SECS = (int)strtol(gtk_entry_get_text (vEntries.noticeSentDelay), (char **) NULL, 10);
	
	printf("Water Diff: %d\nBody Diff: %d\nFire Diff: %d\n", water_tc_differential, body_tc_differential, fire_tc_differential);
	printf("Water PC: %d\nBody PC: %d\nFire PC: %d\n", water_min_detected_pc, body_min_detected_pc, fire_min_detected_pc);

}

static void calibrate_water (GtkWidget *widget, gpointer *data)
{
	set_reference_frame();
	
	GtkWidget *dialog;
	GtkDialogFlags flags = GTK_DIALOG_DESTROY_WITH_PARENT;
	dialog = gtk_message_dialog_new (main_window,
									 flags,
									 GTK_MESSAGE_INFO,
									 GTK_BUTTONS_OK,
									 "Reference Image Set.\nPlace the water in the camera frame click 'OK'");
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	
	// Get the thermal count difference
	int tc_diff = get_tc_difference(40, water_min_detected_pc); // Water
	
	if(tc_diff != 0){
		gchar varStr[8];
		
		if(tc_diff < 0)
			sprintf(varStr, "%d", tc_diff + 10);
		else
			sprintf(varStr, "%d", tc_diff - 5);
		
		gtk_entry_set_text(vEntries.waterDiff, varStr);
	} else {
		printf("Could not detect difference.");	
	}	
}


static void calibrate_body (GtkWidget *widget, gpointer *data)
{
	set_reference_frame();
	
	GtkWidget *dialog;
	GtkDialogFlags flags = GTK_DIALOG_DESTROY_WITH_PARENT;
	dialog = gtk_message_dialog_new (main_window,
									 flags,
									 GTK_MESSAGE_INFO,
									 GTK_BUTTONS_OK,
									 "Reference Image Set.\nPlace your hand in the camera frame click 'OK'");
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	
	// Get the thermal count difference
	int tc_diff = get_tc_difference(100, body_min_detected_pc);
		
	if(tc_diff != 0){
		gchar varStr[8];
		sprintf(varStr, "%d", tc_diff - 5); // Reducing the difference since the average is returned.
		gtk_entry_set_text(vEntries.bodyDiff, varStr);
	} else {
		printf("Could not detect difference.");	
	}	
}


static void calibrate_fire (GtkWidget *widget, gpointer *data)
{
	set_reference_frame();
	
	GtkWidget *dialog;
	GtkDialogFlags flags = GTK_DIALOG_DESTROY_WITH_PARENT;
	dialog = gtk_message_dialog_new (main_window,
									 flags,
									 GTK_MESSAGE_INFO,
									 GTK_BUTTONS_OK,
									 "Reference Image Set.\nPlace some fire in the camera frame click 'OK'");
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	
	// Get the thermal count difference
	int tc_diff = get_tc_difference(300, fire_min_detected_pc); 
			
	if(tc_diff != 0){
		gchar varStr[8];
		sprintf(varStr, "%d", tc_diff - 20); // Reducing the difference since the average is returned.
		gtk_entry_set_text(vEntries.fireDiff, varStr);
	} else {
		printf("Could not detect difference.");	
	}	
}



static void done_settings (GtkWidget *widget, gpointer window)
{
	gtk_widget_destroy(GTK_WIDGET(window));
	start_video_feed();
}


//static void show_settings(GtkWidget *widget, gpointer *data)
static void show_settings()
{
	stop_video_feed();
	
	GtkBuilder *builder;
	GObject *window;
	GObject *button;
	GObject *entry;

	builder = gtk_builder_new ();
	//gtk_builder_set_application(builder, app_ui);
	gtk_builder_add_from_file (builder, "builder-settings.xml", NULL);

	window = gtk_builder_get_object (builder, "settingsWindow");
	gtk_window_set_transient_for ((GtkWindow*)window, main_window);
	gtk_window_set_keep_above((GtkWindow*)window, TRUE);
	gtk_window_set_modal((GtkWindow*)window, TRUE);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

	//--- Detection Variables

	gchar varStr[8];
	sprintf(varStr, "%d", water_tc_differential);
	vEntries.waterDiff = (GtkEntry*)gtk_builder_get_object (builder, "eWaterTcDiff");
	gtk_entry_set_text(vEntries.waterDiff, varStr);

	sprintf(varStr, "%d", body_tc_differential);
	vEntries.bodyDiff = (GtkEntry*)gtk_builder_get_object (builder, "eBodyTcDiff");
	gtk_entry_set_text(vEntries.bodyDiff, varStr);

	sprintf(varStr, "%d", fire_tc_differential);
	vEntries.fireDiff = (GtkEntry*)gtk_builder_get_object (builder, "eFireTcDiff");
	gtk_entry_set_text(vEntries.fireDiff, varStr);

	sprintf(varStr, "%d", water_min_detected_pc);
	vEntries.minWaterPC = (GtkEntry*)gtk_builder_get_object (builder, "eWaterPC");
	gtk_entry_set_text(vEntries.minWaterPC, varStr);

	sprintf(varStr, "%d", body_min_detected_pc);
	vEntries.minBodyPC = (GtkEntry*)gtk_builder_get_object (builder, "eBodyPC");
	gtk_entry_set_text(vEntries.minBodyPC, varStr);

	sprintf(varStr, "%d", fire_min_detected_pc);
	vEntries.minFirePC = (GtkEntry*)gtk_builder_get_object (builder, "eFirePC");
	gtk_entry_set_text(vEntries.minFirePC, varStr);


	//--- Calibrate Variables
	button = gtk_builder_get_object (builder, "btnCalibrateWater");
	g_signal_connect (button, "clicked", G_CALLBACK (calibrate_water), NULL);

	button = gtk_builder_get_object (builder, "btnCalibrateBody");
	g_signal_connect (button, "clicked", G_CALLBACK (calibrate_body), NULL);

	button = gtk_builder_get_object (builder, "btnCalibrateFire");
	g_signal_connect (button, "clicked", G_CALLBACK (calibrate_fire), NULL);


	// --- Delay Variables
	sprintf(varStr, "%d", WATER_DETECTED_SECS);
	vEntries.floodDetectDelay = (GtkEntry*)gtk_builder_get_object (builder, "eFloodDetectDelay");
	gtk_entry_set_text(vEntries.floodDetectDelay, varStr);

	sprintf(varStr, "%d", WATER_DETECTED_CONFIRMED_SECS_TO_SHUTDOWN);
	vEntries.floodConfirmedDelay = (GtkEntry*)gtk_builder_get_object (builder, "eFloodConfirmedDelay");
	gtk_entry_set_text(vEntries.floodConfirmedDelay, varStr);

	sprintf(varStr, "%d", NOTIFCATION_SENT_DELAY_SECS);
	vEntries.noticeSentDelay = (GtkEntry*)gtk_builder_get_object (builder, "eNoticeSentDelay");
	gtk_entry_set_text(vEntries.noticeSentDelay, varStr);


	//--- Capture Image
	captureImage = (GtkImage*)gtk_builder_get_object (builder, "captureImage");
	gtk_image_set_from_file (captureImage, "no-image.gif");
	button = gtk_builder_get_object (builder, "btnCaptureFrame");
	g_signal_connect (button, "clicked", G_CALLBACK (capture_image), captureImage);


	//--- Save
	button = gtk_builder_get_object (builder, "btnSaveSettings");
	g_signal_connect (button, "clicked", G_CALLBACK (save_settings), NULL); 

	//--- Done
	button = gtk_builder_get_object (builder, "btnDoneSettings");
	g_signal_connect (button, "clicked", G_CALLBACK (done_settings), (GtkWindow*)window);

	gtk_widget_show ((GtkWidget*)window);
}

// </editor-fold>





/*********************************** Monitor **********************************/
// <editor-fold>

//enum DemoStatus {START, POSSIBLEFLOOD, FLOODCONFIRMED, WATEROFF, NOTIFCATIONSENT};

void update_demo_status(uint8_t status)
{
	switch(status){
		
		case 0 :
			gtk_image_set_from_file (demoStart, "demo_start_active.png");
			gtk_image_set_from_file (demoPossibleFlooding, "demo_possible_flooding_off.png");
			gtk_image_set_from_file (demoFloodingConfirmed, "demo_flooding_confirmed_off.png");
			break;
			
		case 1 :
			gtk_image_set_from_file (demoStart, "demo_start_on.png");
			gtk_image_set_from_file (demoPossibleFlooding, "demo_possible_flooding_active.png");
			gtk_image_set_from_file (demoFloodingConfirmed, "demo_flooding_confirmed_off.png");
			floodIconUpdated = true;
			break;
			
		case 2 :
			gtk_image_set_from_file (demoPossibleFlooding, "demo_possible_flooding_on.png");
			gtk_image_set_from_file (demoFloodingConfirmed, "demo_flooding_confirmed_active.png");
			floodConfirmedIconUpdated = true;
			break;
			
		case 3 :
			gtk_image_set_from_file (demoFloodingConfirmed, "demo_flooding_confirmed_on.png");
			gtk_image_set_from_file (demoWaterShutOff, "demo_water_shutoff_active.png");
			waterShutoffIconUpdated = true;
			break;
			
		case 4 :
			gtk_image_set_from_file (demoWaterShutOff, "demo_water_shutoff_on.png");
			gtk_image_set_from_file (demoNotificationSent, "demo_notification_sent_active.png");
			noticeSentIconUpdated = true;
			break;
			
		case 5 : // Reset all to off
			gtk_image_set_from_file (demoStart, "demo_start_off.png");
			gtk_image_set_from_file (demoPossibleFlooding, "demo_possible_flooding_off.png");
			gtk_image_set_from_file (demoFloodingConfirmed, "demo_flooding_confirmed_off.png");
			gtk_image_set_from_file (demoWaterShutOff, "demo_water_shutoff_off.png");
			gtk_image_set_from_file (demoNotificationSent, "demo_notification_sent_off.png");
			break;
	
	}
}



void update_monitor_status_labels(char *labelValue)
{	
	if(!_active){
	
		set_gpio_12(0);
		set_gpio_16(0);
		
		if(!_demoFinished)
			update_demo_status(5);
	}
}


//static void toggle_monitor (GtkWidget *widget, gpointer *data)
static void toggle_monitor ()
{	
	_currentIteration = 0;
	_monitorActive = !_monitorActive;
	update_monitor_status_labels(NULL);
	update_demo_status(5);
	
	if(!_monitorActive){
		set_gpio_16(0);
		set_gpio_12(0);
	}
		
}

// </editor-fold>


static gboolean button_press_event( GtkWidget *widget, GdkEventButton *event )
{
  if (event->button == 1) {
	  
	  // Top Left - Settings
	  if(event->x_root < 140 && event->y_root < 110)
		  show_settings();
	  
	  // Bottom Left - Quit
	  if(event->x_root < 140 && event->y_root > 960){
		stop_video_feed();
		gtk_main_quit();
	  }
		
	  // Start
	  if(event->x_root > 110 && event->x_root < 360 && event->y_root > 400 && event->y_root < 520)
		  toggle_monitor();
	  
	  
	  // Video area - toggle video
	  if(event->x_root > 860 && event->x_root < 1600 && event->y_root > 275 && event->y_root < 825){
	  
		  if(_active){
			  stop_video_feed();
		  } else {
			  start_video_feed();
		  }
	  }
		  
  }
  return TRUE;
}


static gboolean key_press_event( GtkWidget *widget, GdkEventKey *event )
{

	// S= Settings
	if(event->keyval == 115)
		show_settings();
	  
	// Q = Quit
	if(event->keyval == 113){
		stop_video_feed();
		gtk_main_quit();
	}
	  
	// Space = Start
	if(event->keyval == 32)
		toggle_monitor();
	return FALSE;
}

/*
int main (int argc, char *argv[])
{
  GtkBuilder *builder;
  GObject *window;
  GObject *button;
  GObject *grid;
  GObject *layout;
  GtkWidget *bgImage;
  
  
  // Initialize the IO
  initializeGpio();
  
  //--- Construct a GtkBuilder instance and load our UI description
  gtk_init (&argc, &argv);
  mainc = g_main_context_default();
  app_ui = gtk_application_new ("org.gnome.example", G_APPLICATION_FLAGS_NONE);
  
  builder = gtk_builder_new ();
  gtk_builder_set_application(builder, app_ui);
  gtk_builder_add_from_file (builder, "builder.xml", NULL);
  //gtk_builder_add_from_file (builder, "builder2.glade", NULL);
    
  //--- Main Window
  window = gtk_builder_get_object (builder, "mainWindow");
  g_signal_connect (window, "destroy", G_CALLBACK (gtk_main_quit), NULL);
  main_window = (GtkWindow*)window;
  //gtk_window_fullscreen(main_window);
  
 
  
  // --- Grid
  grid = gtk_builder_get_object (builder, "grid");
  gtk_widget_set_halign((GtkWidget*)grid, GTK_ALIGN_CENTER);
  gtk_widget_set_valign((GtkWidget*)grid, GTK_ALIGN_CENTER);
  
    
  //--- Capture Image
  captureImage = (GtkImage*)gtk_builder_get_object (builder, "captureImage");
  gtk_image_set_from_file (captureImage, "demo_bg_small.png");
  //gtk_widget_set_halign ((GtkWidget*)captureImage, GTK_ALIGN_CENTER);
  button = gtk_builder_get_object (builder, "btnCaptureFrame");
  g_signal_connect (button, "clicked", G_CALLBACK (capture_image), captureImage);
  
  // --- Video Area
  videoArea = (GtkDrawingArea*)gtk_builder_get_object (builder, "videoArea");
  gtk_widget_set_size_request ((GtkWidget*)videoArea, 757, 568);
  videoFrameBlock = (unsigned char*)malloc(757*568*3);
  
  
  //--- Monitor toggle & status
  statusLabel = (GtkLabel*)gtk_builder_get_object (builder, "statuslabel");
  monitorButton = (GtkButton*)gtk_builder_get_object (builder, "btnStartMonitor");
  g_signal_connect (monitorButton, "clicked", G_CALLBACK (toggle_monitor), NULL);
  
  
  //--- Settings
  button = gtk_builder_get_object (builder, "btnShowSettings");
  g_signal_connect (button, "clicked", G_CALLBACK (show_settings), NULL);
  //--- Quit
  button = gtk_builder_get_object (builder, "quit");
  g_signal_connect (button, "clicked", G_CALLBACK (gtk_main_quit), NULL);
  
  
  
  gtk_main ();
 
  return 0;
}
*/

int main( int argc, char *argv[])
{
    GtkWidget *window;
    GtkWidget *layout;
    GtkWidget *image;
    GtkWidget *button;
	
	// Initialize the IO
	initializeGpio();
	
	gtk_init(&argc, &argv);
	mainc = g_main_context_default();
	
	
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_fullscreen((GtkWindow*)window);
	main_window = (GtkWindow*)window;
	g_signal_connect (window, "button_press_event", G_CALLBACK (button_press_event), NULL);
	g_signal_connect (window, "key_press_event", G_CALLBACK (key_press_event), NULL);
	
    layout = gtk_layout_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER (window), layout);
    gtk_widget_show(layout);

	//-- Background
    image = gtk_image_new_from_file("demo_background.png");
    gtk_layout_put(GTK_LAYOUT(layout), image, 0, 0);

	// -- Logo
	image = gtk_image_new_from_file("demo_logo.png");
    gtk_layout_put(GTK_LAYOUT(layout), image, 0, 0);
		
	// -- Start
	demoStart = (GtkImage*)gtk_image_new_from_file("demo_start_off.png");
    gtk_layout_put(GTK_LAYOUT(layout), demoStart, 0, 401);
	
	// -- Possible Flooding
	demoPossibleFlooding = (GtkImage*)gtk_image_new_from_file("demo_possible_flooding_off.png");
    gtk_layout_put(GTK_LAYOUT(layout), demoPossibleFlooding, 0, 515);
	
	// -- Flooding Confirmed
	demoFloodingConfirmed = (GtkImage*)gtk_image_new_from_file("demo_flooding_confirmed_off.png");
    gtk_layout_put(GTK_LAYOUT(layout), demoFloodingConfirmed, 0, 629);
	
	// -- Water Shutoff
	demoWaterShutOff = (GtkImage*)gtk_image_new_from_file("demo_water_shutoff_off.png");
    gtk_layout_put(GTK_LAYOUT(layout), demoWaterShutOff, 0, 743);
	
	// -- Notification Sent
	demoNotificationSent = (GtkImage*)gtk_image_new_from_file("demo_notification_sent_off.png");
    gtk_layout_put(GTK_LAYOUT(layout), demoNotificationSent, 0, 857);
	
	
	// --- Video Area
	videoArea = (GtkDrawingArea*)gtk_drawing_area_new ();
	gtk_widget_set_size_request ((GtkWidget*)videoArea, 757, 568);
	gtk_layout_put(GTK_LAYOUT(layout), videoArea, 854, 259);
	g_signal_connect (videoArea, "draw", (GCallback) init_video_area, NULL);
	videoFrameBlock = (unsigned char*)malloc(757*568*3);
	//videoFrameBlock = (unsigned char*)malloc(80*60*3);
	
	
	//--- Monitor toggle & status
	//monitorButton = gtk_button_new_with_label("Start");
	//gtk_layout_put(GTK_LAYOUT(layout), monitorButton, 150, 50);
	//g_signal_connect (monitorButton, "clicked", G_CALLBACK (toggle_monitor), NULL);
	
	
    g_signal_connect_swapped(G_OBJECT(window), "destroy",
    G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(window);
	start_video_feed();
    
	gtk_main();

	
	
    return 0;
}