#include <gtk/gtk.h>
#include <sqlite3.h>
#include <time.h>

// Global widgets we'll need to access
typedef struct {
    GtkWidget *window;
    GtkWidget *amount_entry;
    GtkWidget *description_entry;
    GtkWidget *category_combo;
    GtkWidget *payment_type_combo;
    GtkWidget *expense_table;
    GtkWidget *budget_entry;
    GtkWidget *budget_chart;
    GtkWidget *category_chart;
    GtkWidget *payment_chart;
    sqlite3 *db;
    GtkWidget *filter_combo;     // Filter dropdown
    GtkWidget *search_entry;     // Search bar
    GtkWidget *export_button;
    GtkWidget *edit_button;  
    GtkWidget *delete_button;  // Export button
    GtkListStore *expense_store; // For storing filtered results
    GtkWidget *pagination_box;
    GtkWidget *prev_button;
    GtkWidget *next_button;
    GtkWidget *page_label;
    int current_page;
    int total_pages;
    GtkWidget *budget_button;
    GtkWidget *progress_bar;
    double monthly_budget;
    double current_spend;
    GtkTreeSelection *selection;
    gint selected_expense_id;
    GtkTreeIter selected_iter;
} AppData;

// Color definitions for pie charts
typedef struct {
    double r, g, b;
    const char *label;
} ChartColor;

static const ChartColor CATEGORY_COLORS[] = {
    {0.2, 0.6, 0.9, "Food"},         // Blue
    {0.9, 0.2, 0.2, "Transport"},    // Red
    {0.2, 0.8, 0.2, "Entertainment"},// Green
    {0.9, 0.6, 0.2, "Bills"},        // Orange
    {0.6, 0.2, 0.9, "Others"}        // Purple
};

static const ChartColor PAYMENT_COLORS[] = {
    {0.2, 0.7, 0.2, "Cash"},         // Green
    {0.9, 0.3, 0.3, "Credit Card"},  // Red
    {0.3, 0.5, 0.9, "Debit Card"},   // Blue
    {0.9, 0.6, 0.2, "UPI"}           // Orange
};

// Function declarations
static void init_database(sqlite3 *db);
static void add_expense(GtkButton *button, AppData *app);
static void update_charts(AppData *app);
static void export_to_excel(GtkButton *button, AppData *app);
static void set_monthly_budget(GtkButton *button, AppData *app);
static void update_expense_table(AppData *app);
static void init_filter_section(AppData *app, GtkWidget *main_box);
static void filter_changed(GtkComboBox *combo, AppData *app);
static void search_changed(GtkSearchEntry *entry, AppData *app);
static void update_expense_list(AppData *app, const gchar *category, const gchar *search_text);
static void init_expense_table(AppData *app, GtkWidget *main_box);
static void prev_page(GtkButton *button, AppData *app);
static void next_page(GtkButton *button, AppData *app);
static void init_budget_section(AppData *app, GtkWidget *main_box);
static void load_current_budget(AppData *app);
static void update_budget_progress(AppData *app);
static void init_analytics_section(AppData *app, GtkWidget *main_box);
static gboolean draw_category_chart(GtkWidget *widget, cairo_t *cr, AppData *app);
static gboolean draw_payment_chart(GtkWidget *widget, cairo_t *cr, AppData *app);
static void init_form_section(AppData *app, GtkWidget *main_box);
static gboolean show_edit_dialog(AppData *app, gint expense_id);
static void edit_expense(GtkButton *button, AppData *app);
static void delete_expense(GtkButton *button, AppData *app);
static void reset_selection(AppData *app);
static void on_expense_selected(GtkTreeSelection *selection, AppData *app);
static void on_action_clicked(GtkCellRendererText *cell, gchar *path_str, 
                            gchar *new_text, AppData *app);

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    
    AppData app;
    
    // Create main window
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Expense Tracker");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 1200, 600); // Set a fixed height
    gtk_window_set_resizable(GTK_WINDOW(app.window), TRUE); // Allow resizing
    gtk_container_set_border_width(GTK_CONTAINER(app.window), 4);

    // Create main vertical box
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(app.window), main_box);

    // Initialize database
    if (sqlite3_open("expenses.db", &app.db) != SQLITE_OK) {
        g_print("Cannot open database: %s\n", sqlite3_errmsg(app.db));
        return 1;
    }
    init_database(app.db);

    // Initialize all sections in order
    init_form_section(&app, main_box);           // Your existing form section
    init_filter_section(&app, main_box);         // Filter and search section

    // Create a scrolled window for the expense table
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(main_box), scrolled_window, TRUE, TRUE, 0);

    // Initialize the expense table and add it to the scrolled window
    init_expense_table(&app, scrolled_window); // Pass the scrolled window

    init_budget_section(&app, main_box);         // Budget section
    init_analytics_section(&app, main_box);      // Pie charts

    // Connect signals
    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    // Show all widgets
    gtk_widget_show_all(app.window);
    
    // Initial update of all components
    update_expense_list(&app, "All", "");
    update_budget_progress(&app);
    update_charts(&app);
    
    gtk_main();
    
    // Cleanup
    sqlite3_close(app.db);
    
    return 0;
}

// Function to initialize the expense table
static void init_expense_table(AppData *app, GtkWidget *main_box) {
    GtkWidget *frame = gtk_frame_new("Expenses");
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(frame), vbox);

    // Create scrolled window for table
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 300);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    // Create list store with all columns
    app->expense_store = gtk_list_store_new(7,
        G_TYPE_INT,     // ID (hidden)
        G_TYPE_STRING,  // Amount
        G_TYPE_STRING,  // Description
        G_TYPE_STRING,  // Category
        G_TYPE_STRING,  // Payment Type
        G_TYPE_STRING,  // Date
        G_TYPE_STRING   // Actions
    );

    // Create tree view
    app->expense_table = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app->expense_store));
    gtk_container_add(GTK_CONTAINER(scroll), app->expense_table);

    // Create selection
    app->selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->expense_table));
    gtk_tree_selection_set_mode(app->selection, GTK_SELECTION_SINGLE);
    g_signal_connect(G_OBJECT(app->selection), "changed", G_CALLBACK(on_expense_selected), app);

    // Create columns
    const char *titles[] = {"Amount", "Description", "Category", "Payment Type", "Date", "Actions"};
    
    // Add data columns
    for (int i = 0; i < 6; i++) {
        GtkCellRenderer *renderer;
        GtkTreeViewColumn *column;

        if (i == 5) { // Actions column
            renderer = gtk_cell_renderer_text_new();
            g_object_set(renderer, 
                "text", "Edit | Delete",
                "foreground", "blue",
                "editable", TRUE,
                NULL);
            column = gtk_tree_view_column_new_with_attributes(
                titles[i], renderer, "text", i + 1, NULL);
            g_signal_connect(renderer, "edited", G_CALLBACK(on_action_clicked), app);
        } else {
            renderer = gtk_cell_renderer_text_new();
            column = gtk_tree_view_column_new_with_attributes(
                titles[i], renderer, "text", i + 1, NULL);
        }
        
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(app->expense_table), column);
    }

    gtk_box_pack_start(GTK_BOX(main_box), frame, TRUE, TRUE, 0);
}

// Add this function to initialize the database tables
static void init_database(sqlite3 *db) {
    char *err_msg = 0;
    
    // Create expenses table
    const char *sql_expenses = 
        "CREATE TABLE IF NOT EXISTS expenses ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "amount REAL NOT NULL,"
        "description TEXT,"
        "category TEXT NOT NULL,"
        "payment_type TEXT NOT NULL,"
        "date TEXT NOT NULL"
        ");";
    
    // Create budget table
    const char *sql_budget = 
        "CREATE TABLE IF NOT EXISTS budget ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "amount REAL NOT NULL,"
        "month TEXT NOT NULL UNIQUE"
        ");";
    
    if (sqlite3_exec(db, sql_expenses, 0, 0, &err_msg) != SQLITE_OK) {
        g_print("SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    }
    
    if (sqlite3_exec(db, sql_budget, 0, 0, &err_msg) != SQLITE_OK) {
        g_print("SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    }
}

// Add this function to initialize the form section
static void init_form_section(AppData *app, GtkWidget *main_box) {
    GtkWidget *form_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    
    // Amount entry
    app->amount_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->amount_entry), "Amount");
    
    // Description entry
    app->description_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->description_entry), "Description");
    
    // Category dropdown
    app->category_combo = gtk_combo_box_text_new();
    const char *categories[] = {"Food", "Transport", "Entertainment", "Bills", "Others"};
    for (int i = 0; i < 5; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->category_combo), categories[i]);
    }
    
    // Payment type dropdown
    app->payment_type_combo = gtk_combo_box_text_new();
    const char *payment_types[] = {"Cash", "Credit Card", "Debit Card", "UPI"};
    for (int i = 0; i < 4; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->payment_type_combo), payment_types[i]);
    }
    
    // Add expense button
    GtkWidget *add_button = gtk_button_new_with_label("Add Expense");
    GtkStyleContext *context = gtk_widget_get_style_context(add_button);
    gtk_style_context_add_class(context, "suggested-action");
    
    // Pack form elements
    gtk_box_pack_start(GTK_BOX(form_box), app->amount_entry, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(form_box), app->description_entry, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(form_box), app->category_combo, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(form_box), app->payment_type_combo, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(form_box), add_button, FALSE, FALSE, 5);
    
    // Connect add button signal
    g_signal_connect(add_button, "clicked", G_CALLBACK(add_expense), app);
    
    // Add form box to main box
    gtk_box_pack_start(GTK_BOX(main_box), form_box, FALSE, FALSE, 5);
}

static void add_expense(GtkButton *button, AppData *app) {
    const gchar *amount_str = gtk_entry_get_text(GTK_ENTRY(app->amount_entry));
    const gchar *description = gtk_entry_get_text(GTK_ENTRY(app->description_entry));
    const gchar *category = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->category_combo));
    const gchar *payment_type = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->payment_type_combo));

    // Validate input
    if (strlen(amount_str) == 0 || category == NULL || payment_type == NULL) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_CLOSE,
            "Please fill in all required fields (Amount, Category, Payment Type)");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    // Add to database
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO expenses (amount, description, category, payment_type) VALUES (?, ?, ?, ?)";
    
    if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_double(stmt, 1, atof(amount_str));
        sqlite3_bind_text(stmt, 2, description, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, category, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, payment_type, -1, SQLITE_STATIC);
        
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            // Successfully added the expense
            gtk_entry_set_text(GTK_ENTRY(app->amount_entry), "");
            gtk_entry_set_text(GTK_ENTRY(app->description_entry), "");
            gtk_combo_box_set_active(GTK_COMBO_BOX(app->category_combo), -1);
            gtk_combo_box_set_active(GTK_COMBO_BOX(app->payment_type_combo), -1);

            // Update the expense table and charts immediately
            update_expense_list(app, "All", ""); // Refresh the expense list
            update_budget_progress(app); // Update budget progress
            update_charts(app); // Update charts

            // Show success message
            GtkWidget *success_dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_INFO,
                GTK_BUTTONS_OK,
                "Expense added successfully!");
            gtk_dialog_run(GTK_DIALOG(success_dialog));
            gtk_widget_destroy(success_dialog);
        }
        sqlite3_finalize(stmt);
    }
}
static void init_filter_section(AppData *app, GtkWidget *main_box) {
    // Create horizontal box for filter section
    GtkWidget *filter_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    
    // Create filter combo box
    app->filter_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->filter_combo), "All");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->filter_combo), "Food");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->filter_combo), "Transport");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->filter_combo), "Entertainment");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->filter_combo), "Bills");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->filter_combo), "Others");
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->filter_combo), 0);

    // Create search entry
    app->search_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->search_entry), "Search expenses...");

    // Create export button with green background
    app->export_button = gtk_button_new_with_label("Export to Excel");
    GtkStyleContext *context = gtk_widget_get_style_context(app->export_button);
    gtk_style_context_add_class(context, "suggested-action");
    
    // Add CSS for the export button
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        ".suggested-action { background: #28a745; color: white; }"
        ".suggested-action:hover { background: #218838; }", -1, NULL);
    gtk_style_context_add_provider(context,
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    // Pack widgets into filter box
    gtk_box_pack_start(GTK_BOX(filter_box), app->filter_combo, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(filter_box), app->search_entry, TRUE, TRUE, 5);
    gtk_box_pack_end(GTK_BOX(filter_box), app->export_button, FALSE, FALSE, 5);

    // Add filter box to main box
    gtk_box_pack_start(GTK_BOX(main_box), filter_box, FALSE, FALSE, 5);

    // Connect signals
    g_signal_connect(app->filter_combo, "changed", G_CALLBACK(filter_changed), app);
    g_signal_connect(app->search_entry, "search-changed", G_CALLBACK(search_changed), app);
    g_signal_connect(app->export_button, "clicked", G_CALLBACK(export_to_excel), app);
}

static void filter_changed(GtkComboBox *combo, AppData *app) {
    const gchar *selected_category = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
    const gchar *search_text = gtk_entry_get_text(GTK_ENTRY(app->search_entry));
    update_expense_list(app, selected_category, search_text);
}

static void search_changed(GtkSearchEntry *entry, AppData *app) {
    const gchar *search_text = gtk_entry_get_text(GTK_ENTRY(entry));
    const gchar *selected_category = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->filter_combo));
    update_expense_list(app, selected_category, search_text);
}



    static void update_expense_list(AppData *app, const gchar *category, const gchar *search_text) {
    gtk_list_store_clear(app->expense_store);
    
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, amount, description, category, payment_type, date "
                     "FROM expenses WHERE (description LIKE ? OR category LIKE ?) "
                     "ORDER BY date DESC";
    
    if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        char search_pattern[256];
        g_snprintf(search_pattern, sizeof(search_pattern), "%%%s%%", 
                  search_text ? search_text : "");
        
        sqlite3_bind_text(stmt, 1, search_pattern, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, search_pattern, -1, SQLITE_STATIC);
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            GtkTreeIter iter;
            gtk_list_store_append(app->expense_store, &iter);
            
            char amount_str[32];
            snprintf(amount_str, sizeof(amount_str), "%.2f", 
                    sqlite3_column_double(stmt, 1));
            
            gtk_list_store_set(app->expense_store, &iter,
                0, sqlite3_column_int(stmt, 0),     // ID
                1, amount_str,                      // Amount
                2, sqlite3_column_text(stmt, 2),    // Description
                3, sqlite3_column_text(stmt, 3),    // Category
                4, sqlite3_column_text(stmt, 4),    // Payment Type
                5, sqlite3_column_text(stmt, 5),    // Date
                6, "Edit | Delete",                 // Actions
                -1);
        }
        
        sqlite3_finalize(stmt);
    }
}

static void export_to_excel(GtkButton *button, AppData *app) {
    FILE *fp = fopen("expenses.csv", "w");
    if (!fp) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_CLOSE,
            "Failed to create export file");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    // Write CSV header
    fprintf(fp, "Amount,Description,Category,Payment Type,Date\n");

    // Query all expenses
    sqlite3_stmt *stmt;
    const char *sql = "SELECT * FROM expenses ORDER BY date DESC";
    
    if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            fprintf(fp, "%s,%s,%s,%s,%s\n",
                sqlite3_column_text(stmt, 1),  // amount
                sqlite3_column_text(stmt, 2),  // description
                sqlite3_column_text(stmt, 3),  // category
                sqlite3_column_text(stmt, 4),  // payment_type
                sqlite3_column_text(stmt, 5)); // date
        }
        sqlite3_finalize(stmt);
    }

    fclose(fp);

    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_CLOSE,
        "Expenses exported successfully to expenses.csv");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void init_budget_section(AppData *app, GtkWidget *main_box) {
    // Create horizontal box for budget section
    GtkWidget *budget_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    
    // Create label
    GtkWidget *budget_label = gtk_label_new("Set Monthly Budget:");
    
    // Create budget entry
    app->budget_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->budget_entry), "Enter monthly budget");
    
    // Create save budget button with blue background
    app->budget_button = gtk_button_new_with_label("Save Budget");
    GtkStyleContext *context = gtk_widget_get_style_context(app->budget_button);
    gtk_style_context_add_class(context, "suggested-action");
    
    // Add CSS for the button
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        ".suggested-action { background: #007bff; color: white; }"
        ".suggested-action:hover { background: #0056b3; }", -1, NULL);
    gtk_style_context_add_provider(context,
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    // Create progress bar
    app->progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(app->progress_bar), TRUE);
    
    // Pack widgets
    gtk_box_pack_start(GTK_BOX(budget_box), budget_label, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(budget_box), app->budget_entry, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(budget_box), app->budget_button, FALSE, FALSE, 5);
    
    // Add budget box and progress bar to main box
    gtk_box_pack_start(GTK_BOX(main_box), budget_box, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(main_box), app->progress_bar, FALSE, FALSE, 5);

    // Connect signals
    g_signal_connect(app->budget_button, "clicked", G_CALLBACK(set_monthly_budget), app);

    // Load existing budget if any
    load_current_budget(app);
    update_budget_progress(app);
}

static void load_current_budget(AppData *app) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char month[8];
    strftime(month, sizeof(month), "%Y-%m", tm);

    sqlite3_stmt *stmt;
    const char *sql = "SELECT amount FROM budget WHERE month = ?";
    
    if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, month, -1, SQLITE_STATIC);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            app->monthly_budget = sqlite3_column_double(stmt, 0);
            char budget_text[32];
            g_snprintf(budget_text, sizeof(budget_text), "%.2f", app->monthly_budget);
            gtk_entry_set_text(GTK_ENTRY(app->budget_entry), budget_text);
        }
        
        sqlite3_finalize(stmt);
    }
}

static void set_monthly_budget(GtkButton *button, AppData *app) {
    const char *budget_text = gtk_entry_get_text(GTK_ENTRY(app->budget_entry));
    double new_budget = atof(budget_text);
    
    if (new_budget <= 0) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_CLOSE,
            "Please enter a valid budget amount greater than 0");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    // Get current month
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char month[8];
    strftime(month, sizeof(month), "%Y-%m", tm);

    // Check if budget already exists for this month
    sqlite3_stmt *check_stmt;
    const char *check_sql = "SELECT id FROM budget WHERE month = ?";
    int budget_exists = 0;
    
    if (sqlite3_prepare_v2(app->db, check_sql, -1, &check_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(check_stmt, 1, month, -1, SQLITE_STATIC);
        if (sqlite3_step(check_stmt) == SQLITE_ROW) {
            budget_exists = 1;
        }
        sqlite3_finalize(check_stmt);
    }

    if (budget_exists) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_CLOSE,
            "Budget for this month has already been set");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    // Save new budget
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO budget (amount, month) VALUES (?, ?)";
    
    if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_double(stmt, 1, new_budget);
        sqlite3_bind_text(stmt, 2, month, -1, SQLITE_STATIC);
        
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            app->monthly_budget = new_budget;
            update_budget_progress(app);
            
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_INFO,
                GTK_BUTTONS_CLOSE,
                "Monthly budget set successfully!");
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
        }
        
        sqlite3_finalize(stmt);
    }
}

static void update_budget_progress(AppData *app) {
    // Get current month's expenses
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char month[8];
    strftime(month, sizeof(month), "%Y-%m", tm);

    sqlite3_stmt *stmt;
    const char *sql = "SELECT SUM(amount) FROM expenses WHERE strftime('%Y-%m', date) = ?";
    
    if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, month, -1, SQLITE_STATIC);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            app->current_spend = sqlite3_column_double(stmt, 0);
            
            if (app->monthly_budget > 0) {
                double fraction = app->current_spend / app->monthly_budget;
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_bar), 
                    fraction > 1.0 ? 1.0 : fraction);
                
                char progress_text[64];
                g_snprintf(progress_text, sizeof(progress_text), 
                    "%.2f / %.2f (%.1f%%)", 
                    app->current_spend, 
                    app->monthly_budget,
                    fraction * 100);
                gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->progress_bar), progress_text);
            }
        }
        
        sqlite3_finalize(stmt);
    }
}

static void init_analytics_section(AppData *app, GtkWidget *main_box) {
    // Create horizontal box for charts
    GtkWidget *charts_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_set_margin_top(charts_box, 10);
    gtk_widget_set_margin_bottom(charts_box, 10);
    
    // Create frames for each chart
    GtkWidget *category_frame = gtk_frame_new("Spending by Category");
    GtkWidget *payment_frame = gtk_frame_new("Spending by Payment Mode");
    
    // Set frame borders and padding
    gtk_frame_set_shadow_type(GTK_FRAME(category_frame), GTK_SHADOW_ETCHED_IN);
    gtk_frame_set_shadow_type(GTK_FRAME(payment_frame), GTK_SHADOW_ETCHED_IN);
    gtk_widget_set_margin_start(category_frame, 10);
    gtk_widget_set_margin_end(category_frame, 10);
    gtk_widget_set_margin_start(payment_frame, 10);
    gtk_widget_set_margin_end(payment_frame, 10);

    // Create drawing areas for charts
    app->category_chart = gtk_drawing_area_new();
    app->payment_chart = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->category_chart, 300, 300);
    gtk_widget_set_size_request(app->payment_chart, 300, 300);

    // Add drawing areas to frames
    gtk_container_add(GTK_CONTAINER(category_frame), app->category_chart);
    gtk_container_add(GTK_CONTAINER(payment_frame), app->payment_chart);

    // Pack frames into charts box
    gtk_box_pack_start(GTK_BOX(charts_box), category_frame, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(charts_box), payment_frame, TRUE, TRUE, 0);

    // Add charts box to main box
    gtk_box_pack_start(GTK_BOX(main_box), charts_box, FALSE, FALSE, 0);

    // Connect drawing signals
    g_signal_connect(app->category_chart, "draw", G_CALLBACK(draw_category_chart), app);
    g_signal_connect(app->payment_chart, "draw", G_CALLBACK(draw_payment_chart), app);
}

static gboolean draw_category_chart(GtkWidget *widget, cairo_t *cr, AppData *app) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    
    int width = allocation.width;
    int height = allocation.height;
    int size = MIN(width, height);
    double radius = size * 0.35;
    double center_x = width / 2;
    double center_y = height / 2;

    // Get category totals
    double category_totals[5] = {0};
    double total = 0;
    
    sqlite3_stmt *stmt;
    const char *sql = "SELECT category, SUM(amount) FROM expenses GROUP BY category";
    
    if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *category = (const char *)sqlite3_column_text(stmt, 0);
            double amount = sqlite3_column_double(stmt, 1);
            
            for (int i = 0; i < 5; i++) {
                if (strcmp(category, CATEGORY_COLORS[i].label) == 0) {
                    category_totals[i] = amount;
                    total += amount;
                    break;
                }
            }
        }
        sqlite3_finalize(stmt);
    }

    // Draw pie chart
    double start_angle = -G_PI / 2;
    double legend_y = 20;

    for (int i = 0; i < 5; i++) {
        if (category_totals[i] > 0) {
            double slice = 2 * G_PI * category_totals[i] / total;
            
            // Draw slice
            cairo_move_to(cr, center_x, center_y);
            cairo_arc(cr, center_x, center_y, radius, 
                     start_angle, start_angle + slice);
            cairo_close_path(cr);
            
            cairo_set_source_rgb(cr, 
                CATEGORY_COLORS[i].r,
                CATEGORY_COLORS[i].g,
                CATEGORY_COLORS[i].b);
            cairo_fill_preserve(cr);
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_stroke(cr);

            // Draw legend
            cairo_set_source_rgb(cr, 
                CATEGORY_COLORS[i].r,
                CATEGORY_COLORS[i].g,
                CATEGORY_COLORS[i].b);
            cairo_rectangle(cr, width - 150, legend_y, 15, 15);
            cairo_fill(cr);

            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_move_to(cr, width - 130, legend_y + 12);
            char legend_text[100];
            snprintf(legend_text, sizeof(legend_text), "%s (%.1f%%)",
                    CATEGORY_COLORS[i].label,
                    (category_totals[i] / total) * 100);
            cairo_show_text(cr, legend_text);

            legend_y += 25;
            start_angle += slice;
        }
    }

    return TRUE;
}

static gboolean draw_payment_chart(GtkWidget *widget, cairo_t *cr, AppData *app) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    
    int width = allocation.width;
    int height = allocation.height;
    int size = MIN(width, height);
    double radius = size * 0.35;
    double center_x = width / 2;
    double center_y = height / 2;

    // Get payment totals
    double payment_totals[4] = {0};
    double total = 0;
    
    sqlite3_stmt *stmt;
    const char *sql = "SELECT payment_type, SUM(amount) FROM expenses GROUP BY payment_type";
    
    if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *payment_type = (const char *)sqlite3_column_text(stmt, 0);
            double amount = sqlite3_column_double(stmt, 1);
            
            for (int i = 0; i < 4; i++) {
                if (strcmp(payment_type, PAYMENT_COLORS[i].label) == 0) {
                    payment_totals[i] = amount;
                    total += amount;
                    break;
                }
            }
        }
        sqlite3_finalize(stmt);
    }

    // Draw pie chart
    double start_angle = -G_PI / 2;
    double legend_y = 20;

    for (int i = 0; i < 4; i++) {
        if (payment_totals[i] > 0) {
            double slice = 2 * G_PI * payment_totals[i] / total;
            
            // Draw slice
            cairo_move_to(cr, center_x, center_y);
            cairo_arc(cr, center_x, center_y, radius, 
                     start_angle, start_angle + slice);
            cairo_close_path(cr);
            
            cairo_set_source_rgb(cr, 
                PAYMENT_COLORS[i].r,
                PAYMENT_COLORS[i].g,
                PAYMENT_COLORS[i].b);
            cairo_fill_preserve(cr);
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_stroke(cr);

            // Draw legend
            cairo_set_source_rgb(cr, 
                PAYMENT_COLORS[i].r,
                PAYMENT_COLORS[i].g,
                PAYMENT_COLORS[i].b);
            cairo_rectangle(cr, width - 150, legend_y, 15, 15);
            cairo_fill(cr);

            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_move_to(cr, width - 130, legend_y + 12);
            char legend_text[100];
            snprintf(legend_text, sizeof(legend_text), "%s (%.1f%%)",
                    PAYMENT_COLORS[i].label,
                    (payment_totals[i] / total) * 100);
            cairo_show_text(cr, legend_text);

            legend_y += 25;
            start_angle += slice;
        }
    }

    return TRUE;
}

static void update_charts(AppData *app) {
    gtk_widget_queue_draw(app->category_chart);
    gtk_widget_queue_draw(app->payment_chart);
}

static void add_date_filter(AppData *app, GtkWidget *main_box) {
    GtkWidget *date_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    
    GtkWidget *from_label = gtk_label_new("From:");
    GtkWidget *to_label = gtk_label_new("To:");
    
    GtkWidget *from_date = gtk_calendar_new();
    GtkWidget *to_date = gtk_calendar_new();
    
    gtk_box_pack_start(GTK_BOX(date_box), from_label, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(date_box), from_date, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(date_box), to_label, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(date_box), to_date, FALSE, FALSE, 5);
    
    gtk_box_pack_start(GTK_BOX(main_box), date_box, FALSE, FALSE, 5);
}

static void on_expense_selected(GtkTreeSelection *selection, AppData *app) {
    GtkTreeModel *model;
    GtkTreeIter iter;

    // Reset the selected ID
    app->selected_expense_id = -1;
    
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        // Store the selected iterator
        app->selected_iter = iter;
        
        // Get the ID from the model
        gint id;
        gtk_tree_model_get(model, &iter, 0, &id, -1);
        app->selected_expense_id = id;
        
        // Enable buttons
        gtk_widget_set_sensitive(app->edit_button, TRUE);
        gtk_widget_set_sensitive(app->delete_button, TRUE);
        
        g_print("Selected expense ID: %d\n", id); // Debug print
    } else {
        // Disable buttons if nothing is selected
        gtk_widget_set_sensitive(app->edit_button, FALSE);
        gtk_widget_set_sensitive(app->delete_button, FALSE);
    }
}

static void on_action_clicked(GtkCellRendererText *cell, gchar *path_str, 
                            gchar *new_text, AppData *app) {
    GtkTreePath *path = gtk_tree_path_new_from_string(path_str);
    GtkTreeIter iter;
    gint id;
    
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(app->expense_store), &iter, path)) {
        gtk_tree_model_get(GTK_TREE_MODEL(app->expense_store), &iter, 0, &id, -1);
        
        // Create action dialog
        GtkWidget *dialog = gtk_dialog_new_with_buttons("Choose Action",
            GTK_WINDOW(app->window),
            GTK_DIALOG_MODAL,
            "Edit", GTK_RESPONSE_YES,
            "Delete", GTK_RESPONSE_NO,
            "Cancel", GTK_RESPONSE_CANCEL,
            NULL);

        gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        if (response == GTK_RESPONSE_YES) {
            // Edit action
            show_edit_dialog(app, id, iter);
        }
        else if (response == GTK_RESPONSE_NO) {
            // Delete action
            GtkWidget *confirm_dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                GTK_DIALOG_MODAL,
                GTK_MESSAGE_QUESTION,
                GTK_BUTTONS_YES_NO,
                "Are you sure you want to delete this expense?");

            if (gtk_dialog_run(GTK_DIALOG(confirm_dialog)) == GTK_RESPONSE_YES) {
                sqlite3_stmt *stmt;
                const char *sql = "DELETE FROM expenses WHERE id = ?";
                
                if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
                    sqlite3_bind_int(stmt, 1, id);
                    
                    if (sqlite3_step(stmt) == SQLITE_DONE) {
                        gtk_list_store_remove(app->expense_store, &iter);
                        update_budget_progress(app);
                        update_charts(app);

                        // Show success message
                        GtkWidget *success_dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                            GTK_DIALOG_MODAL,
                            GTK_MESSAGE_INFO,
                            GTK_BUTTONS_OK,
                            "Expense deleted successfully!");
                        gtk_dialog_run(GTK_DIALOG(success_dialog));
                        gtk_widget_destroy(success_dialog);
                    }
                    
                    sqlite3_finalize(stmt);
                }
            }
            
            gtk_widget_destroy(confirm_dialog);
        }
    }
    
    gtk_tree_path_free(path);
}

static void edit_expense(GtkButton *button, AppData *app) {
    if (app->selected_expense_id < 0) {
        g_print("No expense selected for editing\n");
        return;
    }
    
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Edit Expense",
        GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Save", GTK_RESPONSE_ACCEPT,
        "Cancel", GTK_RESPONSE_CANCEL,
        NULL);

    // Create form fields
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    // Create entry fields
    GtkWidget *amount_entry = gtk_entry_new();
    GtkWidget *description_entry = gtk_entry_new();
    GtkWidget *category_combo = gtk_combo_box_text_new();
    GtkWidget *payment_combo = gtk_combo_box_text_new();

    // Add categories and payment types
    const char *categories[] = {"Food", "Transport", "Entertainment", "Bills", "Others"};
    const char *payment_types[] = {"Cash", "Credit Card", "Debit Card", "UPI"};
    
    for (int i = 0; i < 5; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(category_combo), categories[i]);
    }
    for (int i = 0; i < 4; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(payment_combo), payment_types[i]);
    }

    // Get current values from the tree view
    GtkTreeModel *model = GTK_TREE_MODEL(app->expense_store);
    gchar *amount, *description, *category, *payment_type;
    gtk_tree_model_get(model, &app->selected_iter,
        1, &amount,
        2, &description,
        3, &category,
        4, &payment_type,
        -1);

    // Set current values
    gtk_entry_set_text(GTK_ENTRY(amount_entry), amount);
    gtk_entry_set_text(GTK_ENTRY(description_entry), description);

    // Set combo box selections
    for (int i = 0; i < 5; i++) {
        if (g_strcmp0(categories[i], category) == 0) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(category_combo), i);
            break;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (g_strcmp0(payment_types[i], payment_type) == 0) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(payment_combo), i);
            break;
        }
    }

    // Add fields to grid
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Amount:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), amount_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Description:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), description_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Category:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), category_combo, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Payment Type:"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), payment_combo, 1, 3, 1, 1);

    gtk_container_add(GTK_CONTAINER(content_area), grid);
    gtk_widget_show_all(dialog);

    // Handle response
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *new_amount = gtk_entry_get_text(GTK_ENTRY(amount_entry));
        const char *new_description = gtk_entry_get_text(GTK_ENTRY(description_entry));
        const char *new_category = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(category_combo));
        const char *new_payment_type = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(payment_combo));

        if (strlen(new_amount) > 0 && strlen(new_description) > 0 && 
            new_category != NULL && new_payment_type != NULL) {
            
            // Update database
            sqlite3_stmt *stmt;
            const char *sql = "UPDATE expenses SET amount = ?, description = ?, "
                            "category = ?, payment_type = ? WHERE id = ?";
            
            if (sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_double(stmt, 1, atof(new_amount));
                sqlite3_bind_text(stmt, 2, new_description, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 3, new_category, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 4, new_payment_type, -1, SQLITE_STATIC);
                sqlite3_bind_int(stmt, 5, app->selected_expense_id);
                
                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    // Update tree view
                    gtk_list_store_set(app->expense_store, &app->selected_iter,
                        1, new_amount,
                        2, new_description,
                        3, new_category,
                        4, new_payment_type,
                        -1);
                    
                    // Update displays
                    update_budget_progress(app);
                    update_charts(app);
                    
                    // Show success message
                    GtkWidget *success_dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                        GTK_MESSAGE_INFO,
                        GTK_BUTTONS_OK,
                        "Expense updated successfully!");
                    gtk_dialog_run(GTK_DIALOG(success_dialog));
                    gtk_widget_destroy(success_dialog);
                }
                
                sqlite3_finalize(stmt);
            }
        }
        
        g_free((gchar *)new_category);
        g_free((gchar *)new_payment_type);
    }

    // Cleanup
    g_free(amount);
    g_free(description);
    g_free(category);
    g_free(payment_type);
    gtk_widget_destroy(dialog);
}
