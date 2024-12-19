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
    GtkWidget *search_entry;
    GtkWidget *budget_chart;
    GtkWidget *category_chart;
    GtkWidget *payment_chart;
    sqlite3 *db;
    GtkWidget *filter_combo;     // Filter dropdown
    GtkWidget *search_entry;     // Search bar
    GtkWidget *export_button;    // Export button
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
static void add_date_filter(AppData *app, GtkWidget *main_box);

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    
    AppData app;
    
    // Create main window
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Expense Tracker");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 1200, 800);
    gtk_container_set_border_width(GTK_CONTAINER(app.window), 4);

    // Create main vertical box
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(app.window), main_box);

    // Create form section
    GtkWidget *form_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    
    // Amount entry
    app.amount_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.amount_entry), "Amount");
    
    // Description entry
    app.description_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.description_entry), "Description");
    
    // Category dropdown
    app.category_combo = gtk_combo_box_text_new();
    const char *categories[] = {"Food", "Transport", "Entertainment", "Bills", "Others"};
    for (int i = 0; i < 5; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.category_combo), categories[i]);
    }
    
    // Payment type dropdown
    app.payment_type_combo = gtk_combo_box_text_new();
    const char *payment_types[] = {"Cash", "Credit Card", "Debit Card", "UPI"};
    for (int i = 0; i < 4; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.payment_type_combo), payment_types[i]);
    }
    
    // Add expense button
    GtkWidget *add_button = gtk_button_new_with_label("Add Expense");
    GtkStyleContext *context = gtk_widget_get_style_context(add_button);
    gtk_style_context_add_class(context, "suggested-action");
    
    // Pack form elements
    gtk_box_pack_start(GTK_BOX(form_box), app.amount_entry, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(form_box), app.description_entry, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(form_box), app.category_combo, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(form_box), app.payment_type_combo, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(form_box), add_button, FALSE, FALSE, 5);
    
    // Add form box to main box
    gtk_box_pack_start(GTK_BOX(main_box), form_box, FALSE, FALSE, 5);

    // Initialize database
    if (sqlite3_open("expenses.db", &app.db) != SQLITE_OK) {
        g_print("Cannot open database: %s\n", sqlite3_errmsg(app.db));
        return 1;
    }
    init_database(app.db);

    // Connect signals
    g_signal_connect(add_button, "clicked", G_CALLBACK(add_expense), &app);
    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Show all widgets
    gtk_widget_show_all(app.window);
    
    // Start GTK main loop
    gtk_main();
    
    // Cleanup
    sqlite3_close(app.db);
    
    return 0;
}

// Initialize database tables
static void init_database(sqlite3 *db) {
    char *err_msg = 0;
    const char *sql = 
        "CREATE TABLE IF NOT EXISTS expenses ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "amount REAL NOT NULL,"
        "description TEXT,"
        "category TEXT NOT NULL,"
        "payment_type TEXT NOT NULL,"
        "date DATETIME NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS budget ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "amount REAL NOT NULL,"
        "month TEXT NOT NULL UNIQUE"
        ");";

    int rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        g_print("SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    }
}

static void add_expense(GtkButton *button, AppData *app) {
    // Get values from form
    const char *amount_str = gtk_entry_get_text(GTK_ENTRY(app->amount_entry));
    const char *description = gtk_entry_get_text(GTK_ENTRY(app->description_entry));
    const char *category = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->category_combo));
    const char *payment_type = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->payment_type_combo));
    
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
    
    // Convert amount to double
    double amount = atof(amount_str);
    if (amount <= 0) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_CLOSE,
            "Please enter a valid amount greater than 0");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    // Get current date
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char date[20];
    strftime(date, sizeof(date), "%Y-%m-%d", tm);
    
    // Prepare SQL statement
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO expenses (amount, description, category, payment_type, date) VALUES (?, ?, ?, ?, ?)";
    
    int rc = sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_print("Failed to prepare statement: %s\n", sqlite3_errmsg(app->db));
        return;
    }
    
    // Bind values
    sqlite3_bind_double(stmt, 1, amount);
    sqlite3_bind_text(stmt, 2, description, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, category, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, payment_type, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, date, -1, SQLITE_STATIC);
    
    // Execute statement
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        g_print("Failed to insert data: %s\n", sqlite3_errmsg(app->db));
    }
    
    // Finalize statement
    sqlite3_finalize(stmt);
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
    sqlite3_stmt *stmt;
    const char *count_sql;
    const char *sql;
    int items_per_page = 15;
    int offset = (app->current_page - 1) * items_per_page;
    
    // First, get total count for pagination
    if (g_strcmp0(category, "All") == 0) {
        count_sql = "SELECT COUNT(*) FROM expenses WHERE description LIKE ?";
        sql = "SELECT * FROM expenses WHERE description LIKE ? ORDER BY date DESC LIMIT ? OFFSET ?";
    } else {
        count_sql = "SELECT COUNT(*) FROM expenses WHERE category = ? AND description LIKE ?";
        sql = "SELECT * FROM expenses WHERE category = ? AND description LIKE ? ORDER BY date DESC LIMIT ? OFFSET ?";
    }

    // Get total count
    sqlite3_stmt *count_stmt;
    int rc = sqlite3_prepare_v2(app->db, count_sql, -1, &count_stmt, NULL);
    if (rc == SQLITE_OK) {
        char search_pattern[256];
        g_snprintf(search_pattern, sizeof(search_pattern), "%%%s%%", search_text ? search_text : "");
        
        if (g_strcmp0(category, "All") == 0) {
            sqlite3_bind_text(count_stmt, 1, search_pattern, -1, SQLITE_STATIC);
        } else {
            sqlite3_bind_text(count_stmt, 1, category, -1, SQLITE_STATIC);
            sqlite3_bind_text(count_stmt, 2, search_pattern, -1, SQLITE_STATIC);
        }
        
        if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            int total_items = sqlite3_column_int(count_stmt, 0);
            app->total_pages = (total_items + items_per_page - 1) / items_per_page;
            if (app->total_pages < 1) app->total_pages = 1;
            
            // Update page label
            char page_text[32];
            g_snprintf(page_text, sizeof(page_text), "Page %d of %d", app->current_page, app->total_pages);
            gtk_label_set_text(GTK_LABEL(app->page_label), page_text);
            
            // Update button sensitivity
            gtk_widget_set_sensitive(app->prev_button, app->current_page > 1);
            gtk_widget_set_sensitive(app->next_button, app->current_page < app->total_pages);
        }
        sqlite3_finalize(count_stmt);
    }

    // Get paginated results
    rc = sqlite3_prepare_v2(app->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_print("Failed to prepare statement: %s\n", sqlite3_errmsg(app->db));
        return;
    }

    // Clear existing list store
    gtk_list_store_clear(app->expense_store);

    // Bind parameters
    char search_pattern[256];
    g_snprintf(search_pattern, sizeof(search_pattern), "%%%s%%", search_text ? search_text : "");
    
    int param_index = 1;
    if (g_strcmp0(category, "All") == 0) {
        sqlite3_bind_text(stmt, param_index++, search_pattern, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_text(stmt, param_index++, category, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, param_index++, search_pattern, -1, SQLITE_STATIC);
    }
    sqlite3_bind_int(stmt, param_index++, items_per_page);
    sqlite3_bind_int(stmt, param_index++, offset);

    // Populate list store with filtered results
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        GtkTreeIter iter;
        gtk_list_store_append(app->expense_store, &iter);
        gtk_list_store_set(app->expense_store, &iter,
            0, sqlite3_column_text(stmt, 1),  // amount
            1, sqlite3_column_text(stmt, 2),  // description
            2, sqlite3_column_text(stmt, 3),  // category
            3, sqlite3_column_text(stmt, 4),  // payment_type
            4, sqlite3_column_text(stmt, 5),  // date
            -1);
    }

    sqlite3_finalize(stmt);
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

static void init_expense_table(AppData *app, GtkWidget *main_box) {
    // Create a scrolled window to contain the table
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled_window, -1, 300);

    // Create list store for the table
    app->expense_store = gtk_list_store_new(5, 
        G_TYPE_STRING,  // Amount
        G_TYPE_STRING,  // Description
        G_TYPE_STRING,  // Category
        G_TYPE_STRING,  // Payment Type
        G_TYPE_STRING   // Date
    );

    // Create tree view
    app->expense_table = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app->expense_store));

    // Create columns
    const char *titles[] = {"Amount", "Description", "Category", "Payment Type", "Date"};
    for (int i = 0; i < 5; i++) {
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
            titles[i], renderer, "text", i, NULL);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_column_set_sort_column_id(column, i);
        gtk_tree_view_append_column(GTK_TREE_VIEW(app->expense_table), column);
    }

    // Add table to scrolled window
    gtk_container_add(GTK_CONTAINER(scrolled_window), app->expense_table);

    // Create pagination controls
    app->pagination_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    app->prev_button = gtk_button_new_with_label("Previous");
    app->next_button = gtk_button_new_with_label("Next");
    app->page_label = gtk_label_new("Page 1");
    app->current_page = 1;

    gtk_box_pack_start(GTK_BOX(app->pagination_box), app->prev_button, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(app->pagination_box), app->page_label, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(app->pagination_box), app->next_button, FALSE, FALSE, 5);

    // Add to main box
    gtk_box_pack_start(GTK_BOX(main_box), scrolled_window, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(main_box), app->pagination_box, FALSE, FALSE, 5);

    // Connect pagination signals
    g_signal_connect(app->prev_button, "clicked", G_CALLBACK(prev_page), app);
    g_signal_connect(app->next_button, "clicked", G_CALLBACK(next_page), app);

    // Initial load of expenses
    update_expense_list(app, "All", "");
}

static void prev_page(GtkButton *button, AppData *app) {
    if (app->current_page > 1) {
        app->current_page--;
        const gchar *category = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->filter_combo));
        const gchar *search_text = gtk_entry_get_text(GTK_ENTRY(app->search_entry));
        update_expense_list(app, category, search_text);
    }
}

static void next_page(GtkButton *button, AppData *app) {
    if (app->current_page < app->total_pages) {
        app->current_page++;
        const gchar *category = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->filter_combo));
        const gchar *search_text = gtk_entry_get_text(GTK_ENTRY(app->search_entry));
        update_expense_list(app, category, search_text);
    }
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
    double start_angle = -M_PI / 2;
    double legend_y = 20;

    for (int i = 0; i < 5; i++) {
        if (category_totals[i] > 0) {
            double slice = 2 * M_PI * category_totals[i] / total;
            
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
    double start_angle = -M_PI / 2;
    double legend_y = 20;

    for (int i = 0; i < 4; i++) {
        if (payment_totals[i] > 0) {
            double slice = 2 * M_PI * payment_totals[i] / total;
            
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








      |         ^
      |         ;
 1041 | // Function to add expense to the database
 1042 | void add_expense(const char *description, const char *category, const char *payment_type, double amount) {
      | ~~~~
main.c:1042:6: error: redefinition of 'add_expense'
 1042 | void add_expense(const char *description, const char *category, const char *payment_type, double amount) {
      |      ^~~~~~~~~~~
main.c:328:6: note: previous definition of 'add_expense' with type 'void(const char *, const char *, const char *, double)'
  328 | void add_expense(const char *description, const char *category, const char *payment_type, double amount) {
      |      ^~~~~~~~~~~
main.c:1071:6: error: redefinition of 'on_add_expense_button_clicked'
 1071 | void on_add_expense_button_clicked(GtkWidget *widget, gpointer data) {
      |      ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~
main.c:306:6: note: previous definition of 'on_add_expense_button_clicked' with type 'void(GtkWidget *, void *)' {aka 'void(struct _GtkWidget *, void *)'}
  306 | void on_add_expense_button_clicked(GtkWidget *widget, gpointer data) {
      |      ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~

tanus@Tanush MINGW64 /c/Users/tanus/Desktop/c
$  gcc main.c -o expense_tracker `pkg-config --cflags --libs gtk+-3.0` -lsqlite3
main.c:20:16: error: duplicate member 'search_entry'
   20 |     GtkWidget *search_entry;     // Search bar
      |                ^~~~~~~~~~~~
main.c: In function 'draw_category_chart':
main.c:760:27: error: 'M_PI' undeclared (first use in this function); did you mean 'G_PI'?
  760 |     double start_angle = -M_PI / 2;
      |                           ^~~~
      |                           G_PI
main.c:760:27: note: each undeclared identifier is reported only once for each function it appears in
main.c: In function 'draw_payment_chart':
main.c:840:27: error: 'M_PI' undeclared (first use in this function); did you mean 'G_PI'?
  840 |     double start_angle = -M_PI / 2;
      |                           ^~~~
      |                           G_PI
